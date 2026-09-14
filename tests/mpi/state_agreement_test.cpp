#include <algorithm>
#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/field_group_space.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <rift/state_snapshot.hpp>
#include <rift/state_store.hpp>
#include <utility>

namespace {

[[nodiscard]] constexpr rift::PhaseId air_phase() noexcept { return rift::PhaseId::from_index(0); }

[[nodiscard]] constexpr rift::PhaseId water_phase() noexcept { return rift::PhaseId::from_index(1); }

[[nodiscard]] rift::PhaseGraphSpecification phase_graph_specification()
{
    return {.phases = {{.name = "water", .physics_key = rift::PhysicsKey{"incompressible"}},
                       {.name = "air", .physics_key = rift::PhysicsKey{"compressible"}}},
            .interfaces = {}};
}

[[nodiscard]] rift::SpaceSpecification state_space_specification()
{
    return {.phase_support_fields = {},
            .geometry = {.continuous_fields = {{.name = "indicators",
                                                .components = rift::UnboundFieldComponents{.count = 2},
                                                .degree = 1}},
                         .discrete_metadata = {{.name = "phase-label",
                                                .geometry_field_group = "indicators",
                                                .component = 0,
                                                .kind = rift::DiscreteGeometryMetadataKind::phase_label}}}};
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::SpaceSnapshot<dim>>
make_space(rift::RiftContext& context, const rift::DofNumbering numbering = rift::DofNumbering::native)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    triangulation->refine_global(2);
    auto mesh = context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(mesh.has_value());
    if (!mesh) {
        return nullptr;
    }
    auto supports = context.create_phase_supports<dim>(
        *mesh, {{.phase = water_phase(), .requested_cells = {}}, {.phase = air_phase(), .requested_cells = {}}});
    boost::ut::expect(supports.has_value());
    if (!supports) {
        return nullptr;
    }
    auto draft = context.create_space_draft<dim>(std::move(*supports), state_space_specification());
    boost::ut::expect(draft.has_value());
    if (!draft) {
        return nullptr;
    }
    boost::ut::expect(context.build_field_spaces(*draft, {.numbering = numbering}).has_value());
    auto space = context.finalize_space(
        *draft, {{.phase = air_phase(), .name = "pressure"}, {.phase = water_phase(), .name = "pressure"}});
    boost::ut::expect(space.has_value());
    return space ? *space : nullptr;
}

template<int dim> void test_component_wise_state_allocation(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context, rift::DofNumbering::component_wise);
    if (space == nullptr) {
        return;
    }
    const auto& field_space = space->geometry_field_spaces().front();
    boost::ut::expect(field_space.locally_owned_dofs().is_contiguous());
    boost::ut::expect(field_space.numbering() == rift::DofNumbering::component_wise);

    auto store = context.create_state_store<dim>(space);
    boost::ut::expect(store.has_value());
    if (!store) {
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): fixed by the successful fixture schema.
    const auto field = space->geometry_field_reference(*space->find_geometry_field("indicators"));
    boost::ut::expect((*store)->accepted()->geometry_field(field).locally_owned_size() ==
                      field_space.locally_owned_dofs().n_elements());
}

[[nodiscard]] bool contains_code(const rift::StateErrors& errors, const rift::StateErrorCode code)
{
    return std::ranges::any_of(errors, [code](const auto& error) { return error.code == code; });
}

template<int dim>
[[nodiscard]] bool
seal_regional_bits(rift::RiftContext& context, const std::shared_ptr<const rift::SpaceSnapshot<dim>>& space,
                   rift::StateStore<dim>& store, const std::uint64_t air_value_bits,
                   const std::uint64_t water_value_bits, const rift::GeometryRevision expected_revision)
{
    auto transaction_result = store.begin_transaction();
    boost::ut::expect(transaction_result.has_value());
    if (!transaction_result) {
        return false;
    }
    auto transaction = std::move(*transaction_result);
    for (const auto& entry : space->state_layout().regional_entries()) {
        const auto reference = space->regional_entry_reference(entry.id);
        const auto bits = entry.phase == air_phase() ? air_value_bits : water_value_bits;
        if (context.this_mpi_process() == entry.owner_rank) {
            // A second owner write deliberately proves last-write-wins.
            boost::ut::expect(transaction.set_regional(reference, 123.0).has_value());
            boost::ut::expect(transaction.set_regional(reference, std::bit_cast<double>(bits)).has_value());
        }
    }
    const auto candidate = transaction.seal();
    boost::ut::expect(candidate.has_value());
    if (!candidate) {
        return false;
    }
    boost::ut::expect((*candidate)->stamp().geometry_revision == expected_revision);
    for (const auto& entry : space->state_layout().regional_entries()) {
        const auto expected = entry.phase == air_phase() ? air_value_bits : water_value_bits;
        boost::ut::expect(std::bit_cast<std::uint64_t>(
                              (*candidate)->regional(space->regional_entry_reference(entry.id))) == expected);
    }
    const auto published = store.publish((*candidate)->reference());
    boost::ut::expect(published.has_value());
    return published.has_value();
}

template<int dim> void test_collective_seal(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    if (space == nullptr) {
        return;
    }
    auto store_result = context.create_state_store<dim>(space);
    boost::ut::expect(store_result.has_value());
    if (!store_result) {
        return;
    }
    auto store = std::move(*store_result);
    auto transaction_result = store->begin_transaction();
    boost::ut::expect(transaction_result.has_value());
    if (!transaction_result) {
        return;
    }
    auto transaction = std::move(*transaction_result);

    // NOLINTBEGIN(bugprone-unchecked-optional-access): fixed by the successful fixture schema.
    const auto geometry_id = *space->find_geometry_field("indicators");
    const auto metadata_id = *space->find_discrete_geometry_metadata("phase-label");
    // NOLINTEND(bugprone-unchecked-optional-access)
    const auto geometry_reference = space->geometry_field_reference(geometry_id);
    const auto metadata_reference = space->discrete_geometry_metadata_reference(metadata_id);
    auto& geometry = transaction.geometry_field(geometry_reference);
    if (geometry.locally_owned_size() > 0U) {
        geometry.local_element(0) = static_cast<double>(context.this_mpi_process() + 1U);
    }
    auto labels = transaction.discrete_geometry_metadata(metadata_reference);
    for (const auto dof : labels.locally_owned_dofs()) {
        labels.set_label(dof, water_phase());
    }

    const std::uint64_t air_bits = 0x8000000000000000ULL;
    const std::uint64_t water_bits = 0x7ff8000000000042ULL;
    const auto entries = space->state_layout().regional_entries();
    for (const auto& entry : entries) {
        boost::ut::expect(entry.owner_rank == entry.id.value() % context.n_mpi_processes());
        const auto reference = space->regional_entry_reference(entry.id);
        const auto bits = entry.phase == air_phase() ? air_bits : water_bits;
        if (context.this_mpi_process() == entry.owner_rank) {
            boost::ut::expect(transaction.set_regional(reference, std::bit_cast<double>(bits)).has_value());
        }
        else {
            const auto rejected = transaction.set_regional(reference, 99.0);
            boost::ut::expect(!rejected.has_value());
            if (!rejected) {
                boost::ut::expect(rejected.error().code == rift::StateErrorCode::not_regional_owner);
            }
        }
    }

    const auto candidate = transaction.seal();
    boost::ut::expect(candidate.has_value());
    if (!candidate) {
        return;
    }
    boost::ut::expect((*candidate)->stamp().geometry_revision == rift::GeometryRevision::from_index(1));
    const auto gathered_revisions =
        dealii::Utilities::MPI::all_gather(context.mpi_communicator(), (*candidate)->stamp().geometry_revision.value());
    boost::ut::expect(std::ranges::all_of(gathered_revisions, [](const auto revision) { return revision == 1U; }));

    const auto& sealed_labels = (*candidate)->discrete_geometry_metadata(metadata_reference);
    for (const auto dof : sealed_labels.locally_owned_dofs()) {
        boost::ut::expect(sealed_labels.label(dof) == water_phase());
    }
    for (const auto dof : sealed_labels.ghost_dofs()) {
        boost::ut::expect(sealed_labels.contains(dof));
        boost::ut::expect(!sealed_labels.locally_owns(dof));
        boost::ut::expect(sealed_labels.label(dof) == water_phase());
    }
    for (const auto& entry : entries) {
        const auto expected = entry.phase == air_phase() ? air_bits : water_bits;
        boost::ut::expect(std::bit_cast<std::uint64_t>(
                              (*candidate)->regional(space->regional_entry_reference(entry.id))) == expected);
    }
    const auto initial_publish = store->publish((*candidate)->reference());
    boost::ut::expect(initial_publish.has_value());
    if (!initial_publish) {
        return;
    }

    constexpr std::uint64_t positive_infinity_bits = 0x7ff0000000000000ULL;
    constexpr std::uint64_t negative_infinity_bits = 0xfff0000000000000ULL;
    constexpr std::uint64_t positive_zero_bits = 0x0000000000000000ULL;
    constexpr std::uint64_t negative_nan_bits = 0xfff80000000000a5ULL;
    const auto revision = rift::GeometryRevision::from_index(1);
    if (!seal_regional_bits(context, space, *store, positive_infinity_bits, negative_infinity_bits, revision)) {
        return;
    }
    static_cast<void>(seal_regional_bits(context, space, *store, positive_zero_bits, negative_nan_bits, revision));
}

template<int dim> void test_collective_store_mismatch(rift::RiftContext& context)
{
    const auto first_space = make_space<dim>(context);
    const auto second_space = make_space<dim>(context);
    if (first_space == nullptr || second_space == nullptr) {
        return;
    }
    const auto selected = context.this_mpi_process() == 0U ? first_space : second_space;
    auto result = context.create_state_store<dim>(selected);
    if (context.n_mpi_processes() == 1U) {
        boost::ut::expect(result.has_value());
    }
    else {
        boost::ut::expect(!result.has_value());
        if (!result) {
            boost::ut::expect(std::ranges::any_of(result.error(), [](const auto& error) {
                return error.code == rift::StateStoreCreationErrorCode::collective_space_mismatch;
            }));
        }
    }
}

template<int dim> void test_expired_store_and_transition_mismatch(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    if (space == nullptr) {
        return;
    }
    auto store_result = context.create_state_store<dim>(space, {.max_pinned_snapshots = 1});
    if (!store_result) {
        boost::ut::expect(false);
        return;
    }
    auto store = std::move(*store_result);
    auto expired_result = store->begin_transaction();
    boost::ut::expect(expired_result.has_value());
    if (!expired_result) {
        return;
    }
    auto expired = std::move(*expired_result);
    store.reset();
    const auto expired_seal = expired.seal();
    boost::ut::expect(!expired_seal.has_value());
    if (!expired_seal) {
        boost::ut::expect(contains_code(expired_seal.error(), rift::StateErrorCode::expired_store));
    }

    auto replacement_result = context.create_state_store<dim>(space, {.max_pinned_snapshots = 1});
    if (!replacement_result) {
        boost::ut::expect(false);
        return;
    }
    auto replacement = std::move(*replacement_result);
    auto first_result = replacement->begin_transaction();
    auto second_result = replacement->begin_transaction();
    if (!first_result || !second_result) {
        boost::ut::expect(false);
        return;
    }
    auto first = std::move(*first_result);
    auto second = std::move(*second_result);
    const auto first_candidate = first.seal();
    boost::ut::expect(first_candidate.has_value());
    if (!first_candidate) {
        return;
    }
    boost::ut::expect(replacement->pin((*first_candidate)->reference()).has_value());
    const auto second_candidate = second.seal();
    boost::ut::expect(second_candidate.has_value());
    if (!second_candidate) {
        return;
    }

    const auto selected_reference =
        context.this_mpi_process() == 0U ? (*first_candidate)->reference() : (*second_candidate)->reference();
    const auto publish = replacement->publish(selected_reference);
    if (context.n_mpi_processes() == 1U) {
        boost::ut::expect(publish.has_value());
    }
    else {
        boost::ut::expect(!publish.has_value());
        if (!publish) {
            boost::ut::expect(contains_code(publish.error(), rift::StateErrorCode::collective_operation_mismatch));
            boost::ut::expect(replacement->accepted()->stamp().snapshot_id == rift::StateSnapshotId::from_index(0));
            boost::ut::expect(replacement->lookup((*first_candidate)->reference()).has_value());
            boost::ut::expect(replacement->lookup((*second_candidate)->reference()).has_value());
        }
    }
}

template<int dim> void test_collective_seal_mismatch(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    if (space == nullptr) {
        return;
    }
    auto store_result = context.create_state_store<dim>(space);
    boost::ut::expect(store_result.has_value());
    if (!store_result) {
        return;
    }
    auto store = std::move(*store_result);
    auto first_result = store->begin_transaction();
    auto second_result = store->begin_transaction();
    boost::ut::expect(first_result.has_value() && second_result.has_value());
    if (!first_result || !second_result) {
        return;
    }
    auto first = std::move(*first_result);
    auto second = std::move(*second_result);
    auto& selected = context.this_mpi_process() == 0U ? first : second;
    const auto sealed = selected.seal();
    if (context.n_mpi_processes() == 1U) {
        boost::ut::expect(sealed.has_value());
    }
    else {
        boost::ut::expect(!sealed.has_value());
        if (!sealed) {
            boost::ut::expect(contains_code(sealed.error(), rift::StateErrorCode::collective_operation_mismatch));
            boost::ut::expect(selected.active());
        }
        const auto retry = first.seal();
        boost::ut::expect(retry.has_value());
        if (retry) {
            boost::ut::expect((*retry)->stamp().snapshot_id == rift::StateSnapshotId::from_index(1));
            boost::ut::expect((*retry)->stamp().geometry_revision == rift::GeometryRevision::from_index(0));
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);
    const auto graph =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);
    static rift::RiftContext* context_ptr = nullptr;
    static bool graph_created = false;
    context_ptr = &actual_context;
    graph_created = graph.has_value();

    [[maybe_unused]] const suite<"StateAgreement"> suite = [] {
        auto& context = *context_ptr;
        "sealing agrees on geometry revisions, ghost labels, and exact regional bits"_test = [&context] {
            expect(graph_created);
            test_collective_seal<2>(context);
            test_collective_seal<3>(context);
        };
        "component-wise spaces retain vector-compatible rank ownership"_test = [&context] {
            test_component_wise_state_allocation<2>(context);
            test_component_wise_state_allocation<3>(context);
        };
        "store creation rejects ranks selecting different finalized spaces"_test = [&context] {
            test_collective_store_mismatch<2>(context);
            test_collective_store_mismatch<3>(context);
        };
        "expired transactions and different publish references fail coherently"_test = [&context] {
            test_expired_store_and_transition_mismatch<2>(context);
            test_expired_store_and_transition_mismatch<3>(context);
        };
        "sealing different transaction identities fails coherently"_test = [&context] {
            test_collective_seal_mismatch<2>(context);
            test_collective_seal_mismatch<3>(context);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
