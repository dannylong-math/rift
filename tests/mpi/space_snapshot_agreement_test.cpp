#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <format>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <string>
#include <utility>
#include <variant>
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

[[nodiscard]] std::vector<rift::PhaseSupportSpecification> support_specifications()
{
    return {{.phase = water_phase(), .requested_cells = {}}, {.phase = air_phase(), .requested_cells = {}}};
}

[[nodiscard]] rift::SpaceSpecification space_specification()
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

template<int dim> [[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim> [[nodiscard]] std::optional<rift::SpaceDraft<dim>> make_built_draft(rift::RiftContext& context)
{
    const auto mesh = make_snapshot<dim>(context);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    auto supports = context.create_phase_supports<dim>(mesh, support_specifications());
    boost::ut::expect(supports.has_value());
    if (!supports.has_value()) {
        return std::nullopt;
    }
    auto draft = context.create_space_draft<dim>(std::move(*supports), space_specification());
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return std::nullopt;
    }
    const auto built = context.build_field_spaces(*draft);
    boost::ut::expect(built.has_value());
    return built.has_value() ? std::optional<rift::SpaceDraft<dim>>{std::move(*draft)} : std::nullopt;
}

[[nodiscard]] std::size_t count_code(const rift::SpaceFinalizationErrors& errors,
                                     const rift::SpaceFinalizationErrorCode code)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(errors, [code](const auto& error) { return error.code == code; }));
}

void test_equivalent_permutations_and_owners(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context);
    if (!draft.has_value()) {
        return;
    }
    std::vector<rift::RegionalEntrySpecification> regional_entries{
        {.phase = water_phase(), .name = "zeta"},
        {.phase = air_phase(), .name = "beta"},
        {.phase = air_phase(), .name = "alpha"},
        {.phase = water_phase(), .name = "alpha"},
    };
    if (context.this_mpi_process() % 2U == 1U) {
        std::ranges::reverse(regional_entries);
    }
    const auto result = context.finalize_space(*draft, std::move(regional_entries));
    boost::ut::expect(result.has_value());
    if (!result.has_value()) {
        return;
    }

    const auto entries = (*result)->state_layout().regional_entries();
    boost::ut::expect(entries.size() == std::size_t{4});
    std::vector<std::string> local_signature;
    for (const auto& entry : entries) {
        boost::ut::expect(entry.owner_rank == entry.id.value() % context.n_mpi_processes());
        local_signature.push_back(std::format("{}:{}:{}:{}:{}", entry.phase.value(), entry.name, entry.owner_rank,
                                              entry.offset, entry.cardinality));
    }
    const auto gathered = dealii::Utilities::MPI::all_gather(context.mpi_communicator(), local_signature);
    boost::ut::expect(std::ranges::all_of(
        gathered, [&local_signature](const auto& signature) { return signature == local_signature; }));
}

void test_regional_schema_mismatch(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context);
    if (!draft.has_value()) {
        return;
    }
    auto name = std::string{"reference"};
    if (context.this_mpi_process() != 0) {
        name = "different";
    }
    const auto result = context.finalize_space(*draft, {{.phase = air_phase(), .name = std::move(name)}});
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(!result.has_value());
    boost::ut::expect(draft->active());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::regional_schema_mismatch) ==
                          context.n_mpi_processes() - 1U);
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::layout_mismatch) ==
                          context.n_mpi_processes() - 1U);
    }
}

void test_regional_cardinality_mismatch(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context);
    if (!draft.has_value()) {
        return;
    }
    std::vector<rift::RegionalEntrySpecification> regional_entries{{.phase = air_phase(), .name = "first"}};
    if (context.this_mpi_process() != 0) {
        regional_entries.push_back({.phase = water_phase(), .name = "second"});
    }
    const auto result = context.finalize_space(*draft, std::move(regional_entries));
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(!result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::regional_schema_mismatch) ==
                          context.n_mpi_processes() - 1U);
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::layout_mismatch) ==
                          context.n_mpi_processes() - 1U);
    }
}

void test_epoch_mismatch(rift::RiftContext& context)
{
    auto first = make_built_draft<2>(context);
    auto second = make_built_draft<2>(context);
    if (!first.has_value() || !second.has_value()) {
        return;
    }
    auto& selected = context.this_mpi_process() == 0 ? *first : *second;
    const auto result = context.finalize_space(selected);
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(!result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(
            count_code(result.error(), rift::SpaceFinalizationErrorCode::collective_space_epoch_mismatch) ==
            context.n_mpi_processes() - 1U);
    }
}

void test_draft_state_mismatch(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context);
    if (!draft.has_value()) {
        return;
    }
    std::optional<rift::SpaceDraft<2>> retained;
    if (context.this_mpi_process() != 0) {
        retained.emplace(std::move(*draft));
    }
    const auto result = context.finalize_space(*draft);
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }
    boost::ut::expect(!result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::inactive_draft) ==
                          context.n_mpi_processes() - 1U);
        boost::ut::expect(
            count_code(result.error(), rift::SpaceFinalizationErrorCode::collective_draft_state_mismatch) ==
            context.n_mpi_processes() - 1U);
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::layout_mismatch) ==
                          context.n_mpi_processes() - 1U);
    }
}

void test_dimension_layout_mismatch(rift::RiftContext& context)
{
    auto draft_2d = make_built_draft<2>(context);
    auto draft_3d = make_built_draft<3>(context);
    if (!draft_2d.has_value() || !draft_3d.has_value()) {
        return;
    }
    if (context.n_mpi_processes() == 1 || context.this_mpi_process() == 0) {
        const auto result = context.finalize_space(*draft_2d);
        if (context.n_mpi_processes() == 1) {
            boost::ut::expect(result.has_value());
        }
        else {
            boost::ut::expect(!result.has_value());
            if (!result.has_value()) {
                boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::layout_mismatch) ==
                                  context.n_mpi_processes() - 1U);
            }
        }
    }
    else {
        const auto result = context.finalize_space(*draft_3d);
        boost::ut::expect(!result.has_value());
        if (!result.has_value()) {
            boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::layout_mismatch) ==
                              context.n_mpi_processes() - 1U);
        }
    }
}

void test_rank_local_validation_error(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context);
    if (!draft.has_value()) {
        return;
    }
    const auto phase = context.this_mpi_process() == 0 ? rift::PhaseId::from_index(99) : air_phase();
    const auto result = context.finalize_space(*draft, {{.phase = phase, .name = "regional"}});
    boost::ut::expect(!result.has_value());
    if (!result.has_value()) {
        boost::ut::expect(count_code(result.error(), rift::SpaceFinalizationErrorCode::unknown_phase) ==
                          std::size_t{1});
        const auto found = std::ranges::find_if(result.error(), [](const auto& error) {
            return error.code == rift::SpaceFinalizationErrorCode::unknown_phase;
        });
        boost::ut::expect(found != result.error().end());
        if (found != result.error().end()) {
            boost::ut::expect(found->rank == 0U);
            const auto* subject = std::get_if<rift::RegionalEntryErrorSubject>(&found->subject);
            boost::ut::expect(subject != nullptr);
            if (subject != nullptr) {
                boost::ut::expect(subject->specification.phase == rift::PhaseId::from_index(99));
                boost::ut::expect(subject->specification.name == "regional");
            }
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext actual_context(argc, argv);
    const auto graph_result =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);

    static rift::RiftContext* context_ptr = nullptr;
    static bool graph_created = false;
    context_ptr = &actual_context;
    graph_created = graph_result.has_value();

    [[maybe_unused]] const suite<"SpaceSnapshotAgreement"> suite = [] {
        auto& context = *context_ptr;
        "equivalent regional permutations publish identical layouts and owners"_test = [&context] {
            expect(graph_created);
            test_equivalent_permutations_and_owners(context);
        };
        "regional schema differences fail coherently"_test = [&context] { test_regional_schema_mismatch(context); };
        "regional cardinality differences fail coherently"_test = [&context] {
            test_regional_cardinality_mismatch(context);
        };
        "different draft epochs fail coherently"_test = [&context] { test_epoch_mismatch(context); };
        "different draft lifecycle states fail coherently"_test = [&context] { test_draft_state_mismatch(context); };
        "different dimensions produce a layout mismatch"_test = [&context] { test_dimension_layout_mismatch(context); };
        "one rank local validation error retains its typed subject"_test = [&context] {
            test_rank_local_validation_error(context);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
