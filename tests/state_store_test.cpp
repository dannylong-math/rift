#include <algorithm>
#include <bit>
#include <boost/ut.hpp>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <rift/state_snapshot.hpp>
#include <rift/state_store.hpp>
#include <rift/state_transaction.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
    return {.phase_support_fields =
                {
                    {.phase = water_phase(), .name = "velocity", .component_count = 2, .degree = 1},
                    {.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1},
                },
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
make_space(rift::RiftContext& context, const bool include_regional = true,
           const rift::DofNumbering numbering = rift::DofNumbering::native)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    triangulation->refine_global(1);
    auto mesh = context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(mesh.has_value());
    if (!mesh.has_value()) {
        return nullptr;
    }

    std::vector<dealii::CellId> owned_cells;
    for (const auto& cell : (*mesh)->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned_cells.push_back(cell->id());
        }
    }
    auto supports =
        context.create_phase_supports<dim>(*mesh, {{.phase = water_phase(), .requested_cells = owned_cells},
                                                   {.phase = air_phase(), .requested_cells = std::move(owned_cells)}});
    boost::ut::expect(supports.has_value());
    if (!supports.has_value()) {
        return nullptr;
    }

    auto draft = context.create_space_draft<dim>(std::move(*supports), state_space_specification());
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return nullptr;
    }
    boost::ut::expect(context.build_field_spaces(*draft, {.numbering = numbering}).has_value());
    const auto regional_entries =
        include_regional
            ? std::vector<rift::RegionalEntrySpecification>{{.phase = air_phase(), .name = "thermodynamic-pressure"}}
            : std::vector<rift::RegionalEntrySpecification>{};
    auto space = context.finalize_space(*draft, regional_entries);
    boost::ut::expect(space.has_value());
    return space.has_value() ? *space : nullptr;
}

template<int dim>
[[nodiscard]] std::unique_ptr<rift::StateStore<dim>>
make_store(rift::RiftContext& context, const std::shared_ptr<const rift::SpaceSnapshot<dim>>& space,
           const std::size_t pin_capacity = 0)
{
    auto result = context.create_state_store<dim>(space, {.max_pinned_snapshots = pin_capacity});
    boost::ut::expect(result.has_value());
    return result.has_value() ? std::move(*result) : nullptr;
}

[[nodiscard]] bool contains_code(const rift::StateErrors& errors, const rift::StateErrorCode code)
{
    return std::ranges::any_of(errors, [code](const auto& error) { return error.code == code; });
}

void copy_assign_labels(rift::PhaseLabelMetadata& destination, const rift::PhaseLabelMetadata& source)
{
    destination = source;
}

[[nodiscard]] bool equal_snapshot_ids(const rift::StateSnapshotId left, const rift::StateSnapshotId right) noexcept
{
    return std::is_eq(left <=> right);
}

[[nodiscard]] bool equal_store_ids(const rift::StateStoreId left, const rift::StateStoreId right) noexcept
{
    return std::is_eq(left <=> right);
}

static_assert(!std::is_copy_constructible_v<rift::StateSnapshot<2>>);
static_assert(!std::is_move_constructible_v<rift::StateSnapshot<2>>);
static_assert(!std::is_copy_constructible_v<rift::StateStore<2>>);
static_assert(!std::is_move_constructible_v<rift::StateStore<2>>);
static_assert(!std::is_copy_constructible_v<rift::StateTransaction<2>>);
static_assert(std::is_move_constructible_v<rift::StateTransaction<2>>);
static_assert(!std::is_move_assignable_v<rift::StateTransaction<2>>);

template<int dim> void test_root_allocation(rift::RiftContext& context)
{
    const auto runtime_index = static_cast<std::uint64_t>(context.this_mpi_process());
    const auto lower = rift::StateSnapshotId::from_index(runtime_index);
    const auto upper = rift::StateSnapshotId::from_index(runtime_index + 1U);
    boost::ut::expect(std::is_lt(lower <=> upper));
    boost::ut::expect(std::is_gt(upper <=> lower));
    boost::ut::expect(equal_snapshot_ids(lower, rift::StateSnapshotId::from_index(runtime_index)));
    const auto space = make_space<dim>(context);
    if (space == nullptr) {
        return;
    }
    auto store = make_store(context, space);
    if (store == nullptr) {
        return;
    }

    const auto root = store->accepted();
    const auto next_store_id = rift::StateStoreId::from_index(store->id().value() + 1U);
    boost::ut::expect(std::is_lt(store->id() <=> next_store_id));
    boost::ut::expect(std::is_gt(next_store_id <=> store->id()));
    boost::ut::expect(equal_store_ids(store->id(), rift::StateStoreId::from_index(store->id().value())));
    boost::ut::expect(store->id() == root->stamp().store_id);
    boost::ut::expect(root->stamp().space_epoch == space->epoch());
    boost::ut::expect(root->stamp().snapshot_id == rift::StateSnapshotId::from_index(0));
    boost::ut::expect(!root->stamp().base_snapshot_id.has_value());
    boost::ut::expect(root->stamp().geometry_revision == rift::GeometryRevision::from_index(0));
    boost::ut::expect(store->previous() == nullptr);
    boost::ut::expect(store->transient() == nullptr);

    const auto root_reference = root->reference();
    boost::ut::expect(root_reference == root->reference());
    boost::ut::expect(root_reference != rift::StateSnapshotReference{.store_id = next_store_id,
                                                                     .snapshot_id = root_reference.snapshot_id});
    boost::ut::expect(root_reference != rift::StateSnapshotReference{.store_id = root_reference.store_id,
                                                                     .snapshot_id = rift::StateSnapshotId::from_index(
                                                                         root_reference.snapshot_id.value() + 1U)});
    boost::ut::expect(&store->space() == space.get());

    const auto support_id = space->find_phase_support_field(air_phase(), "pressure");
    const auto geometry_id = space->find_geometry_field("indicators");
    const auto metadata_id = space->find_discrete_geometry_metadata("phase-label");
    const auto regional_id = space->find_regional_entry(air_phase(), "thermodynamic-pressure");
    boost::ut::expect(support_id.has_value() && geometry_id.has_value() && metadata_id.has_value() &&
                      regional_id.has_value());
    if (!support_id || !geometry_id || !metadata_id || !regional_id) {
        return;
    }

    const auto& support = root->phase_support_field(space->phase_support_field_reference(*support_id));
    const auto& geometry = root->geometry_field(space->geometry_field_reference(*geometry_id));
    boost::ut::expect(support.size() == space->phase_support_field_space(*support_id).dof_handler().n_dofs());
    boost::ut::expect(geometry.size() == space->geometry_field_space(*geometry_id).dof_handler().n_dofs());
    for (std::size_t index = 0; index < support.locally_owned_size(); ++index) {
        boost::ut::expect(support.local_element(index) == 0.0);
    }
    for (std::size_t index = 0; index < geometry.locally_owned_size(); ++index) {
        boost::ut::expect(geometry.local_element(index) == 0.0);
    }

    const auto& labels = root->discrete_geometry_metadata(space->discrete_geometry_metadata_reference(*metadata_id));
    boost::ut::expect(labels.locally_owned_dofs().n_elements() > 0U);
    for (const auto dof : labels.locally_owned_dofs()) {
        boost::ut::expect(labels.contains(dof));
        boost::ut::expect(labels.locally_owns(dof));
        boost::ut::expect(!labels.label(dof).has_value());
    }
    boost::ut::expect(root->regional(space->regional_entry_reference(*regional_id)) == 0.0);
    boost::ut::expect(store->lookup(root->reference()).has_value());

    const auto foreign = store->lookup({.store_id = rift::StateStoreId::from_index(store->id().value() + 1),
                                        .snapshot_id = root->stamp().snapshot_id});
    boost::ut::expect(!foreign.has_value());
    if (!foreign.has_value()) {
        boost::ut::expect(foreign.error().code == rift::StateErrorCode::foreign_snapshot);
    }

    using boost::ut::throws;
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(root->geometry_field(
            {.space_epoch = rift::SpaceEpoch::from_index(space->epoch().value() + 1), .field_group = *geometry_id}));
    }));
    boost::ut::expect(
        throws<std::out_of_range>([&] { static_cast<void>(labels.label(labels.locally_owned_dofs().size())); }));
}

template<int dim> void test_transaction_and_revision(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    auto store = space == nullptr ? nullptr : make_store(context, space);
    if (store == nullptr) {
        return;
    }
    // These names are fixed by the successful fixture schema above.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    const auto support_id = *space->find_phase_support_field(air_phase(), "pressure");
    const auto geometry_id = *space->find_geometry_field("indicators");
    const auto metadata_id = *space->find_discrete_geometry_metadata("phase-label");
    const auto regional_id = *space->find_regional_entry(air_phase(), "thermodynamic-pressure");
    // NOLINTEND(bugprone-unchecked-optional-access)
    const auto support_reference = space->phase_support_field_reference(support_id);
    const auto geometry_reference = space->geometry_field_reference(geometry_id);
    const auto metadata_reference = space->discrete_geometry_metadata_reference(metadata_id);
    const auto regional_reference = space->regional_entry_reference(regional_id);

    auto transaction_result = store->begin_transaction();
    boost::ut::expect(transaction_result.has_value());
    if (!transaction_result.has_value()) {
        return;
    }
    auto transaction = std::move(*transaction_result);
    const auto transaction_id = transaction.id();
    boost::ut::expect(transaction.active());
    boost::ut::expect(transaction.base().reference() == store->accepted()->reference());
    auto& support = transaction.phase_support_field(support_reference);
    auto& geometry = transaction.geometry_field(geometry_reference);
    support.local_element(0) = 4.0;
    geometry.local_element(0) = -0.0;
    auto labels = transaction.discrete_geometry_metadata(metadata_reference);
    const auto first_label_dof = labels.locally_owned_dofs().nth_index_in_set(0);
    labels.set_label(first_label_dof, water_phase());
    boost::ut::expect(labels.label(first_label_dof) == water_phase());
    boost::ut::expect(transaction.set_regional(regional_reference, -17.25).has_value());

    auto candidate_result = transaction.seal();
    boost::ut::expect(candidate_result.has_value());
    boost::ut::expect(!transaction.active());
    if (!candidate_result.has_value()) {
        return;
    }
    const auto& candidate = *candidate_result;
    boost::ut::expect(candidate->stamp().snapshot_id == rift::StateSnapshotId::from_index(1));
    boost::ut::expect(candidate->stamp().base_snapshot_id == rift::StateSnapshotId::from_index(0));
    boost::ut::expect(candidate->stamp().geometry_revision == rift::GeometryRevision::from_index(1));
    boost::ut::expect(candidate->phase_support_field(support_reference).local_element(0) == 4.0);
    boost::ut::expect(std::bit_cast<std::uint64_t>(candidate->geometry_field(geometry_reference).local_element(0)) ==
                      std::bit_cast<std::uint64_t>(-0.0));
    boost::ut::expect(candidate->discrete_geometry_metadata(metadata_reference).label(first_label_dof) ==
                      water_phase());
    boost::ut::expect(candidate->regional(regional_reference) == -17.25);
    boost::ut::expect(store->accepted()->phase_support_field(support_reference).local_element(0) == 0.0);
    boost::ut::expect(store->transient() == candidate);
    boost::ut::expect(transaction.id() == transaction_id);
    boost::ut::expect(boost::ut::throws<std::logic_error>([&transaction] { static_cast<void>(transaction.base()); }));

    auto publish = store->publish(candidate->reference());
    boost::ut::expect(publish.has_value());
    boost::ut::expect(store->accepted() == candidate);
    boost::ut::expect(store->previous()->stamp().snapshot_id == rift::StateSnapshotId::from_index(0));
    boost::ut::expect(store->transient() == nullptr);

    auto unchanged_result = store->begin_transaction();
    boost::ut::expect(unchanged_result.has_value());
    if (!unchanged_result.has_value()) {
        return;
    }
    auto unchanged = std::move(*unchanged_result);
    unchanged.phase_support_field(support_reference).local_element(0) = 9.0;
    const auto unchanged_candidate = unchanged.seal();
    boost::ut::expect(unchanged_candidate.has_value());
    if (unchanged_candidate.has_value()) {
        boost::ut::expect((*unchanged_candidate)->stamp().geometry_revision == candidate->stamp().geometry_revision);
    }

    const auto seal_and_publish_geometry_value = [&](const std::uint64_t bits, const std::uint64_t expected_revision) {
        auto special_result = store->begin_transaction();
        boost::ut::expect(special_result.has_value());
        if (!special_result) {
            return false;
        }
        auto special = std::move(*special_result);
        special.geometry_field(geometry_reference).local_element(0) = std::bit_cast<double>(bits);
        auto special_candidate = special.seal();
        boost::ut::expect(special_candidate.has_value());
        if (!special_candidate) {
            return false;
        }
        boost::ut::expect(std::bit_cast<std::uint64_t>(
                              (*special_candidate)->geometry_field(geometry_reference).local_element(0)) == bits);
        boost::ut::expect((*special_candidate)->stamp().geometry_revision ==
                          rift::GeometryRevision::from_index(expected_revision));
        const auto published = store->publish((*special_candidate)->reference());
        boost::ut::expect(published.has_value());
        return published.has_value();
    };

    constexpr std::uint64_t positive_infinity_bits = 0x7ff0000000000000ULL;
    constexpr std::uint64_t first_nan_bits = 0x7ff8000000000042ULL;
    constexpr std::uint64_t second_nan_bits = 0x7ff80000000000a5ULL;
    if (!seal_and_publish_geometry_value(positive_infinity_bits, 2)) {
        return;
    }
    if (!seal_and_publish_geometry_value(first_nan_bits, 3)) {
        return;
    }

    auto stable_nan_result = store->begin_transaction();
    boost::ut::expect(stable_nan_result.has_value());
    if (!stable_nan_result) {
        return;
    }
    auto stable_nan = std::move(*stable_nan_result);
    auto stable_nan_candidate = stable_nan.seal();
    boost::ut::expect(stable_nan_candidate.has_value());
    if (!stable_nan_candidate) {
        return;
    }
    boost::ut::expect((*stable_nan_candidate)->stamp().geometry_revision == rift::GeometryRevision::from_index(3));
    boost::ut::expect(
        std::bit_cast<std::uint64_t>((*stable_nan_candidate)->geometry_field(geometry_reference).local_element(0)) ==
        first_nan_bits);
    boost::ut::expect(store->publish((*stable_nan_candidate)->reference()).has_value());

    if (!seal_and_publish_geometry_value(second_nan_bits, 4)) {
        return;
    }

    auto abandoned_result = store->begin_transaction();
    boost::ut::expect(abandoned_result.has_value());
    if (abandoned_result.has_value()) {
        auto abandoned = std::move(*abandoned_result);
        auto moved = std::move(abandoned);
        // NOLINTNEXTLINE(bugprone-use-after-move): the public moved-from contract is intentionally tested.
        boost::ut::expect(!abandoned.active());
        boost::ut::expect(moved.active());
        moved.abandon();
        moved.abandon();
        boost::ut::expect(!moved.active());
    }
}

template<int dim> void test_retention_and_errors(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    auto store = space == nullptr ? nullptr : make_store(context, space, 1);
    if (store == nullptr) {
        return;
    }

    auto first_transaction = store->begin_transaction();
    auto sibling_transaction = store->begin_transaction();
    boost::ut::expect(first_transaction.has_value() && sibling_transaction.has_value());
    if (!first_transaction || !sibling_transaction) {
        return;
    }
    auto first = std::move(*first_transaction);
    auto sibling = std::move(*sibling_transaction);
    const auto first_candidate = first.seal();
    boost::ut::expect(first_candidate.has_value());
    if (!first_candidate) {
        return;
    }
    boost::ut::expect(store->pin((*first_candidate)->reference()).has_value());
    boost::ut::expect(store->pin((*first_candidate)->reference()).has_value());

    const auto sibling_candidate = sibling.seal();
    boost::ut::expect(sibling_candidate.has_value());
    if (!sibling_candidate) {
        return;
    }
    const auto capacity = store->pin((*sibling_candidate)->reference());
    boost::ut::expect(!capacity.has_value());
    if (!capacity) {
        boost::ut::expect(contains_code(capacity.error(), rift::StateErrorCode::pin_capacity_exceeded));
    }
    boost::ut::expect(store->publish((*first_candidate)->reference()).has_value());
    const auto stale = store->publish((*sibling_candidate)->reference());
    boost::ut::expect(!stale.has_value());
    if (!stale) {
        boost::ut::expect(contains_code(stale.error(), rift::StateErrorCode::stale_candidate));
    }

    const auto pinned_discard = store->discard((*first_candidate)->reference());
    boost::ut::expect(!pinned_discard.has_value());
    if (!pinned_discard) {
        boost::ut::expect(contains_code(pinned_discard.error(), rift::StateErrorCode::snapshot_pinned));
    }
    boost::ut::expect(store->unpin((*first_candidate)->reference()).has_value());
    boost::ut::expect(store->unpin((*first_candidate)->reference()).has_value());

    const auto accepted_discard = store->discard(store->accepted()->reference());
    boost::ut::expect(!accepted_discard.has_value());
    if (!accepted_discard) {
        boost::ut::expect(contains_code(accepted_discard.error(), rift::StateErrorCode::invalid_snapshot_transition));
    }
    boost::ut::expect(store->discard((*sibling_candidate)->reference()).has_value());
    const auto missing = store->lookup((*sibling_candidate)->reference());
    boost::ut::expect(!missing.has_value());
    if (!missing) {
        boost::ut::expect(missing.error().code == rift::StateErrorCode::snapshot_not_retained);
    }

    const auto foreign_reference =
        rift::StateSnapshotReference{.store_id = rift::StateStoreId::from_index(store->id().value() + 100),
                                     .snapshot_id = rift::StateSnapshotId::from_index(0)};
    const auto foreign_publish = store->publish(foreign_reference);
    boost::ut::expect(!foreign_publish.has_value());
    if (!foreign_publish) {
        boost::ut::expect(contains_code(foreign_publish.error(), rift::StateErrorCode::foreign_snapshot));
    }

    const auto missing_reference =
        rift::StateSnapshotReference{.store_id = store->id(), .snapshot_id = rift::StateSnapshotId::from_index(9999)};
    const auto expect_error = [](const auto& result, const rift::StateErrorCode code) {
        boost::ut::expect(!result.has_value());
        if (!result) {
            boost::ut::expect(contains_code(result.error(), code));
        }
    };
    expect_error(store->publish(missing_reference), rift::StateErrorCode::snapshot_not_retained);
    expect_error(store->pin(missing_reference), rift::StateErrorCode::snapshot_not_retained);
    expect_error(store->unpin(missing_reference), rift::StateErrorCode::snapshot_not_retained);
    expect_error(store->discard(missing_reference), rift::StateErrorCode::snapshot_not_retained);

    expect_error(store->pin(foreign_reference), rift::StateErrorCode::foreign_snapshot);
    expect_error(store->unpin(foreign_reference), rift::StateErrorCode::foreign_snapshot);
    expect_error(store->discard(foreign_reference), rift::StateErrorCode::foreign_snapshot);
    boost::ut::expect(!store->publish(store->previous()->reference()).has_value());
    boost::ut::expect(!store->discard(store->previous()->reference()).has_value());
}

template<int dim> void test_reference_and_metadata_validation(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context);
    auto store = space == nullptr ? nullptr : make_store(context, space);
    if (store == nullptr) {
        return;
    }

    // These names are fixed by the successful fixture schema above.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    const auto support_reference =
        space->phase_support_field_reference(*space->find_phase_support_field(air_phase(), "pressure"));
    const auto geometry_reference = space->geometry_field_reference(*space->find_geometry_field("indicators"));
    const auto metadata_reference =
        space->discrete_geometry_metadata_reference(*space->find_discrete_geometry_metadata("phase-label"));
    const auto regional_reference =
        space->regional_entry_reference(*space->find_regional_entry(air_phase(), "thermodynamic-pressure"));
    // NOLINTEND(bugprone-unchecked-optional-access)
    const auto wrong_epoch = rift::SpaceEpoch::from_index(space->epoch().value() + 1);
    const auto invalid_support = rift::PhaseSupportFieldGroupId::from_index(9999);
    const auto invalid_geometry = rift::GeometryFieldGroupId::from_index(9999);
    const auto invalid_metadata = rift::DiscreteGeometryMetadataId::from_index(9999);
    const auto invalid_regional = rift::RegionalEntryId::from_index(9999);
    const auto root = store->accepted();

    using boost::ut::throws;
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(
            root->phase_support_field({.space_epoch = wrong_epoch, .field_group = support_reference.field_group}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(root->phase_support_field({.space_epoch = space->epoch(), .field_group = invalid_support}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(root->geometry_field({.space_epoch = space->epoch(), .field_group = invalid_geometry}));
    }));
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(
            root->discrete_geometry_metadata({.space_epoch = wrong_epoch, .metadata = metadata_reference.metadata}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(
            root->discrete_geometry_metadata({.space_epoch = space->epoch(), .metadata = invalid_metadata}));
    }));
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(
            root->regional({.space_epoch = wrong_epoch, .regional_entry = regional_reference.regional_entry}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(root->regional({.space_epoch = space->epoch(), .regional_entry = invalid_regional}));
    }));

    const auto& labels = root->discrete_geometry_metadata(metadata_reference);
    const auto outside = labels.locally_owned_dofs().size();
    boost::ut::expect(!labels.contains(outside));
    boost::ut::expect(!labels.locally_owns(outside));
    std::optional<dealii::types::global_dof_index> untracked;
    for (dealii::types::global_dof_index dof = 0; dof < outside; ++dof) {
        if (!labels.contains(dof)) {
            untracked = dof;
            break;
        }
    }
    boost::ut::expect(untracked.has_value());
    if (untracked) {
        boost::ut::expect(throws<std::out_of_range>([&] { static_cast<void>(labels.label(*untracked)); }));
    }
    auto copied_labels = labels;
    auto assigned_labels = labels;
    assigned_labels = copied_labels;
    copy_assign_labels(assigned_labels, assigned_labels);
    auto moved_labels = std::move(copied_labels);
    copied_labels = std::move(moved_labels);

    auto transaction_result = store->begin_transaction();
    boost::ut::expect(transaction_result.has_value());
    if (!transaction_result) {
        return;
    }
    auto transaction = std::move(*transaction_result);
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(transaction.phase_support_field(
            {.space_epoch = wrong_epoch, .field_group = support_reference.field_group}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(
            transaction.phase_support_field({.space_epoch = space->epoch(), .field_group = invalid_support}));
    }));
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(
            transaction.geometry_field({.space_epoch = wrong_epoch, .field_group = geometry_reference.field_group}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(transaction.geometry_field({.space_epoch = space->epoch(), .field_group = invalid_geometry}));
    }));
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(transaction.discrete_geometry_metadata(
            {.space_epoch = wrong_epoch, .metadata = metadata_reference.metadata}));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(
            transaction.discrete_geometry_metadata({.space_epoch = space->epoch(), .metadata = invalid_metadata}));
    }));
    boost::ut::expect(throws<std::invalid_argument>([&] {
        static_cast<void>(transaction.set_regional(
            {.space_epoch = wrong_epoch, .regional_entry = regional_reference.regional_entry}, 1.0));
    }));
    boost::ut::expect(throws<std::out_of_range>([&] {
        static_cast<void>(
            transaction.set_regional({.space_epoch = space->epoch(), .regional_entry = invalid_regional}, 1.0));
    }));

    auto mutable_labels = transaction.discrete_geometry_metadata(metadata_reference);
    boost::ut::expect(throws<std::out_of_range>([&] { mutable_labels.set_label(outside, air_phase()); }));
    const auto owned = mutable_labels.locally_owned_dofs().nth_index_in_set(0);
    boost::ut::expect(
        throws<std::invalid_argument>([&] { mutable_labels.set_label(owned, rift::PhaseId::from_index(9999)); }));
    mutable_labels.set_label(owned, std::nullopt);
}

template<int dim> void test_inactive_expired_and_transient_lifecycles(rift::RiftContext& context)
{
    const auto space = make_space<dim>(context, false);
    auto store = space == nullptr ? nullptr : make_store(context, space, 2);
    if (store == nullptr) {
        return;
    }

    auto first_result = store->begin_transaction();
    boost::ut::expect(first_result.has_value());
    if (!first_result) {
        return;
    }
    auto first = std::move(*first_result);
    // Change only compact geometry metadata so revision detection does not
    // rely on a continuous geometry vector.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): fixed by the successful fixture schema.
    const auto metadata_id = *space->find_discrete_geometry_metadata("phase-label");
    auto first_labels = first.discrete_geometry_metadata(space->discrete_geometry_metadata_reference(metadata_id));
    first_labels.set_label(first_labels.locally_owned_dofs().nth_index_in_set(0), air_phase());
    const auto first_candidate = first.seal();
    boost::ut::expect(first_candidate.has_value());
    const auto inactive = first.seal();
    boost::ut::expect(!inactive.has_value());
    if (!inactive) {
        boost::ut::expect(contains_code(inactive.error(), rift::StateErrorCode::inactive_transaction));
    }

    auto second_result = store->begin_transaction();
    boost::ut::expect(second_result.has_value());
    if (!second_result) {
        return;
    }
    auto second = std::move(*second_result);
    const auto second_candidate = second.seal();
    boost::ut::expect(second_candidate.has_value());
    if (first_candidate && second_candidate) {
        boost::ut::expect(!store->lookup((*first_candidate)->reference()).has_value());
        boost::ut::expect(store->lookup((*second_candidate)->reference()).has_value());
        boost::ut::expect(store->pin((*second_candidate)->reference()).has_value());
        boost::ut::expect(store->publish((*second_candidate)->reference()).has_value());
    }

    auto publish_next = [&store]() -> std::shared_ptr<const rift::StateSnapshot<dim>> {
        auto transaction_result = store->begin_transaction();
        boost::ut::expect(transaction_result.has_value());
        if (!transaction_result) {
            return nullptr;
        }
        auto transaction = std::move(*transaction_result);
        auto candidate = transaction.seal();
        boost::ut::expect(candidate.has_value());
        if (!candidate) {
            return nullptr;
        }
        boost::ut::expect(store->publish((*candidate)->reference()).has_value());
        return *candidate;
    };
    const auto third_candidate = publish_next();
    if (second_candidate) {
        boost::ut::expect(store->unpin((*second_candidate)->reference()).has_value());
        boost::ut::expect(store->pin((*second_candidate)->reference()).has_value());
    }
    const auto fourth_candidate = publish_next();
    boost::ut::expect(third_candidate != nullptr && fourth_candidate != nullptr);
    if (store->previous() != nullptr) {
        const auto previous_discard = store->discard(store->previous()->reference());
        boost::ut::expect(!previous_discard.has_value());
        if (!previous_discard) {
            boost::ut::expect(
                contains_code(previous_discard.error(), rift::StateErrorCode::invalid_snapshot_transition));
        }
    }

    auto fifth_result = store->begin_transaction();
    boost::ut::expect(fifth_result.has_value());
    if (!fifth_result || !second_candidate) {
        return;
    }
    auto fifth = std::move(*fifth_result);
    const auto fifth_candidate = fifth.seal();
    boost::ut::expect(fifth_candidate.has_value());
    boost::ut::expect(store->unpin((*second_candidate)->reference()).has_value());
    if (fifth_candidate) {
        boost::ut::expect(!store->lookup((*fifth_candidate)->reference()).has_value());
    }
    boost::ut::expect(store->transient() == *second_candidate);

    auto expired_result = store->begin_transaction();
    boost::ut::expect(expired_result.has_value());
    if (!expired_result) {
        return;
    }
    auto expired = std::move(*expired_result);
    store.reset();
    boost::ut::expect(boost::ut::throws<std::logic_error>([&expired] { static_cast<void>(expired.base()); }));
}

void test_creation_errors_and_3d(rift::RiftContext& context)
{
    const auto null_store = context.create_state_store<2>(nullptr);
    boost::ut::expect(!null_store.has_value());
    if (!null_store) {
        boost::ut::expect(null_store.error().size() == std::size_t{1});
        boost::ut::expect(null_store.error().front().code == rift::StateStoreCreationErrorCode::null_space);
    }

    const auto null_store_3d = context.create_state_store<3>(nullptr);
    boost::ut::expect(!null_store_3d.has_value());

    const auto space = make_space<3>(context, true, rift::DofNumbering::component_wise);
    auto store = space == nullptr ? nullptr : make_store(context, space);
    if (store != nullptr) {
        boost::ut::expect(store->accepted()->stamp().space_epoch == space->epoch());
        boost::ut::expect(store->begin_transaction().has_value());
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

    [[maybe_unused]] const suite<"StateStore"> suite = [] {
        auto& context = *context_ptr;
        "root state allocates every category with explicit zero or unassigned values"_test = [&context] {
            expect(graph_created);
            test_root_allocation<2>(context);
            test_root_allocation<3>(context);
        };
        "a transaction isolates mutation and seals an exact immutable geometry revision"_test = [&context] {
            test_transaction_and_revision<2>(context);
            test_transaction_and_revision<3>(context);
        };
        "publication, staleness, pins, and bounded retention preserve store roles"_test = [&context] {
            test_retention_and_errors<2>(context);
            test_retention_and_errors<3>(context);
        };
        "store creation reports typed errors and supports 3D state"_test = [&context] {
            test_creation_errors_and_3d(context);
        };
        "state references and compact metadata reject invalid public indices"_test = [&context] {
            test_reference_and_metadata_validation<2>(context);
            test_reference_and_metadata_validation<3>(context);
        };
        "inactive, expired, and superseded candidates retain explicit lifecycle behavior"_test = [&context] {
            test_inactive_expired_and_transient_lifecycles<2>(context);
            test_inactive_expired_and_transient_lifecycles<3>(context);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
