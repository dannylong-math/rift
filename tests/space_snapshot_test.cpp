#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
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

template<int dim> [[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::hyper_cube(*triangulation);
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim>
[[nodiscard]] rift::PhaseSupportSet<dim>
make_full_supports(rift::RiftContext& context, const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot)
{
    std::vector<dealii::CellId> locally_owned_cells;
    for (const auto& cell : snapshot->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            locally_owned_cells.push_back(cell->id());
        }
    }
    auto result = context.create_phase_supports<dim>(
        snapshot, {{.phase = water_phase(), .requested_cells = locally_owned_cells},
                   {.phase = air_phase(), .requested_cells = std::move(locally_owned_cells)}});
    boost::ut::expect(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] rift::SpaceSpecification complete_space_specification()
{
    return {.phase_support_fields =
                {
                    {.phase = water_phase(), .name = "velocity", .component_count = 2, .degree = 1},
                    {.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1},
                },
            .geometry = {
                .continuous_fields =
                    {
                        {.name = "potentials",
                         .components = rift::PhaseBoundFieldComponents{.phases = {water_phase(), air_phase()}},
                         .degree = 1},
                        {.name = "indicators", .components = rift::UnboundFieldComponents{.count = 2}, .degree = 1},
                    },
                .discrete_metadata = {{.name = "phase-label",
                                       .geometry_field_group = "indicators",
                                       .component = 1,
                                       .kind = rift::DiscreteGeometryMetadataKind::phase_label}}}};
}

[[nodiscard]] rift::SpaceSpecification minimal_space_specification()
{
    return {.phase_support_fields = {},
            .geometry = {.continuous_fields = {{.name = "indicator",
                                                .components = rift::UnboundFieldComponents{.count = 1},
                                                .degree = 1}},
                         .discrete_metadata = {}}};
}

template<int dim>
[[nodiscard]] std::optional<rift::SpaceDraft<dim>> make_built_draft(rift::RiftContext& context,
                                                                    rift::SpaceSpecification specification)
{
    const auto mesh = make_snapshot<dim>(context);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    auto draft = context.create_space_draft<dim>(make_full_supports(context, mesh), std::move(specification));
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

static_assert(!std::is_copy_constructible_v<rift::SpaceSnapshot<2>>);
static_assert(!std::is_copy_assignable_v<rift::SpaceSnapshot<2>>);
static_assert(!std::is_move_constructible_v<rift::SpaceSnapshot<2>>);
static_assert(!std::is_move_assignable_v<rift::SpaceSnapshot<2>>);
static_assert(std::is_same_v<rift::SpaceSnapshotResult<2>::value_type, std::shared_ptr<const rift::SpaceSnapshot<2>>>);

void test_complete_layout(rift::RiftContext& context)
{
    auto draft = make_built_draft<2>(context, complete_space_specification());
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }
    const auto epoch = draft->epoch();
    const auto mesh_id = draft->phase_supports().mesh_snapshot().id();
    auto result = context.finalize_space(*draft, {{.phase = water_phase(), .name = "thermodynamic-pressure"},
                                                  {.phase = air_phase(), .name = "pressure-mode"},
                                                  {.phase = air_phase(), .name = "thermodynamic-pressure"}});
    boost::ut::expect(result.has_value());
    boost::ut::expect(!draft->active());
    if (!result.has_value()) {
        return;
    }

    const auto& snapshot = **result;
    const auto& layout = snapshot.state_layout();
    boost::ut::expect(snapshot.epoch() == epoch);
    boost::ut::expect(snapshot.phase_supports().mesh_snapshot().id() == mesh_id);
    boost::ut::expect(snapshot.canonical_schema().phase_support_fields().size() == std::size_t{2});
    boost::ut::expect(snapshot.phase_support_field_spaces().size() == std::size_t{2});
    boost::ut::expect(snapshot.geometry_field_spaces().size() == std::size_t{2});

    // One unrefined quadrilateral has four scalar Q1 DoFs. The independent
    // expected prefix is therefore 4, 8, 8, 8, 4, 1, 1, 1.
    boost::ut::expect(layout.phase_support_fields().size() == std::size_t{2});
    boost::ut::expect(layout.phase_support_field(rift::PhaseSupportFieldGroupId::from_index(0)).offset ==
                      std::uint64_t{0});
    boost::ut::expect(layout.phase_support_field(rift::PhaseSupportFieldGroupId::from_index(0)).cardinality ==
                      std::uint64_t{4});
    boost::ut::expect(layout.phase_support_field(rift::PhaseSupportFieldGroupId::from_index(1)).offset ==
                      std::uint64_t{4});
    boost::ut::expect(layout.phase_support_field(rift::PhaseSupportFieldGroupId::from_index(1)).cardinality ==
                      std::uint64_t{8});
    boost::ut::expect(layout.geometry_fields().size() == std::size_t{2});
    boost::ut::expect(layout.geometry_field(rift::GeometryFieldGroupId::from_index(0)).offset == std::uint64_t{12});
    boost::ut::expect(layout.geometry_field(rift::GeometryFieldGroupId::from_index(0)).cardinality == std::uint64_t{8});
    boost::ut::expect(layout.geometry_field(rift::GeometryFieldGroupId::from_index(1)).offset == std::uint64_t{20});
    boost::ut::expect(layout.geometry_field(rift::GeometryFieldGroupId::from_index(1)).cardinality == std::uint64_t{8});
    boost::ut::expect(layout.discrete_geometry_metadata().size() == std::size_t{1});
    boost::ut::expect(layout.discrete_geometry_metadata(rift::DiscreteGeometryMetadataId::from_index(0)).offset ==
                      std::uint64_t{28});
    boost::ut::expect(layout.discrete_geometry_metadata(rift::DiscreteGeometryMetadataId::from_index(0)).cardinality ==
                      std::uint64_t{4});
    boost::ut::expect(layout.regional_entries().size() == std::size_t{3});
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(0)).offset == std::uint64_t{32});
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(1)).offset == std::uint64_t{33});
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(2)).offset == std::uint64_t{34});
    boost::ut::expect(layout.total_cardinality() == std::uint64_t{35});

    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(0)).phase == air_phase());
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(0)).name == "pressure-mode");
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(1)).phase == air_phase());
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(1)).name == "thermodynamic-pressure");
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(2)).phase == water_phase());
    boost::ut::expect(layout.regional_entry(rift::RegionalEntryId::from_index(2)).name == "thermodynamic-pressure");
    boost::ut::expect(std::ranges::all_of(layout.regional_entries(), [](const auto& entry) {
        return entry.owner_rank == 0U && entry.cardinality == std::uint64_t{1};
    }));
}

template<int dim> void test_discovery_and_identity_lookup(rift::RiftContext& context)
{
    auto draft = make_built_draft<dim>(context, complete_space_specification());
    if (!draft.has_value()) {
        return;
    }
    const auto epoch = draft->epoch();
    auto result = context.finalize_space(*draft, {{.phase = air_phase(), .name = "thermodynamic-pressure"}});
    boost::ut::expect(result.has_value());
    if (!result.has_value()) {
        return;
    }
    const auto& snapshot = **result;
    boost::ut::expect(snapshot.epoch() == epoch);
    boost::ut::expect(snapshot.canonical_schema().geometry_fields().size() == std::size_t{2});
    boost::ut::expect(snapshot.phase_supports().active());
    boost::ut::expect(snapshot.phase_support_field_spaces().size() == std::size_t{2});
    boost::ut::expect(snapshot.geometry_field_spaces().size() == std::size_t{2});
    const auto pressure = snapshot.find_phase_support_field(air_phase(), "pressure");
    const auto indicators = snapshot.find_geometry_field("indicators");
    const auto labels = snapshot.find_discrete_geometry_metadata("phase-label");
    const auto regional = snapshot.find_regional_entry(air_phase(), "thermodynamic-pressure");
    boost::ut::expect(pressure.has_value() && indicators.has_value() && labels.has_value() && regional.has_value());
    if (!pressure.has_value() || !indicators.has_value() || !labels.has_value() || !regional.has_value()) {
        return;
    }
    boost::ut::expect(snapshot.phase_support_field_space(*pressure).descriptor().name == "pressure");
    boost::ut::expect(snapshot.geometry_field_space(*indicators).descriptor().name == "indicators");
    boost::ut::expect(snapshot.state_layout().phase_support_field(*pressure).field_group == *pressure);
    boost::ut::expect(snapshot.state_layout().geometry_field(*indicators).field_group == *indicators);
    boost::ut::expect(snapshot.state_layout().discrete_geometry_metadata(*labels).metadata == *labels);
    boost::ut::expect(snapshot.state_layout().regional_entry(*regional).name == "thermodynamic-pressure");

    boost::ut::expect(!snapshot.find_phase_support_field(water_phase(), "pressure").has_value());
    boost::ut::expect(!snapshot.find_phase_support_field(air_phase(), "missing").has_value());
    boost::ut::expect(!snapshot.find_geometry_field("missing").has_value());
    boost::ut::expect(!snapshot.find_discrete_geometry_metadata("missing").has_value());
    boost::ut::expect(!snapshot.find_regional_entry(water_phase(), "thermodynamic-pressure").has_value());
    boost::ut::expect(!snapshot.find_regional_entry(air_phase(), "missing").has_value());

    using boost::ut::throws;
    boost::ut::expect(throws<std::out_of_range>([&snapshot] {
        static_cast<void>(snapshot.phase_support_field_space(rift::PhaseSupportFieldGroupId::from_index(9)));
    }));
    boost::ut::expect(throws<std::out_of_range>(
        [&snapshot] { static_cast<void>(snapshot.geometry_field_space(rift::GeometryFieldGroupId::from_index(9))); }));
    boost::ut::expect(throws<std::out_of_range>([&snapshot] {
        static_cast<void>(snapshot.state_layout().phase_support_field(rift::PhaseSupportFieldGroupId::from_index(9)));
    }));
    boost::ut::expect(throws<std::out_of_range>([&snapshot] {
        static_cast<void>(snapshot.state_layout().geometry_field(rift::GeometryFieldGroupId::from_index(9)));
    }));
    boost::ut::expect(throws<std::out_of_range>([&snapshot] {
        static_cast<void>(
            snapshot.state_layout().discrete_geometry_metadata(rift::DiscreteGeometryMetadataId::from_index(9)));
    }));
    boost::ut::expect(throws<std::out_of_range>([&snapshot] {
        static_cast<void>(snapshot.state_layout().regional_entry(rift::RegionalEntryId::from_index(9)));
    }));
}

template<int dim> void test_regional_validation_retry(rift::RiftContext& context)
{
    auto draft = make_built_draft<dim>(context, minimal_space_specification());
    if (!draft.has_value()) {
        return;
    }
    const std::string invalid_utf8{static_cast<char>(0xC3), static_cast<char>(0x28)};
    auto invalid = context.finalize_space(*draft, {{.phase = air_phase(), .name = "duplicate"},
                                                   {.phase = air_phase(), .name = "duplicate"},
                                                   {.phase = water_phase(), .name = ""},
                                                   {.phase = water_phase(), .name = invalid_utf8},
                                                   {.phase = rift::PhaseId::from_index(99), .name = "unknown"}});
    boost::ut::expect(!invalid.has_value());
    boost::ut::expect(draft->active());
    boost::ut::expect(draft->field_spaces_built());
    if (!invalid.has_value()) {
        boost::ut::expect(count_code(invalid.error(), rift::SpaceFinalizationErrorCode::empty_name) == std::size_t{1});
        boost::ut::expect(count_code(invalid.error(), rift::SpaceFinalizationErrorCode::invalid_name_encoding) ==
                          std::size_t{1});
        boost::ut::expect(count_code(invalid.error(), rift::SpaceFinalizationErrorCode::duplicate_name) ==
                          std::size_t{2});
        boost::ut::expect(count_code(invalid.error(), rift::SpaceFinalizationErrorCode::unknown_phase) ==
                          std::size_t{1});
        boost::ut::expect(std::ranges::all_of(invalid.error(), [](const auto& error) {
            return std::holds_alternative<rift::RegionalEntryErrorSubject>(error.subject) && !error.message.empty();
        }));
    }

    const auto corrected = context.finalize_space(*draft, {{.phase = water_phase(), .name = "corrected"}});
    boost::ut::expect(corrected.has_value());
    boost::ut::expect(!draft->active());
}

template<int dim> void test_unbuilt_retry_and_single_use(rift::RiftContext& context)
{
    const auto mesh = make_snapshot<dim>(context);
    if (mesh == nullptr) {
        return;
    }
    auto draft = context.create_space_draft<dim>(make_full_supports(context, mesh), minimal_space_specification());
    boost::ut::expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }
    const auto unbuilt = context.finalize_space(*draft);
    boost::ut::expect(!unbuilt.has_value());
    boost::ut::expect(draft->active());
    if (!unbuilt.has_value()) {
        boost::ut::expect(count_code(unbuilt.error(), rift::SpaceFinalizationErrorCode::field_spaces_not_built) ==
                          std::size_t{1});
    }
    boost::ut::expect(context.build_field_spaces(*draft).has_value());
    const auto published = context.finalize_space(*draft);
    boost::ut::expect(published.has_value());
    const auto repeated = context.finalize_space(*draft);
    boost::ut::expect(!repeated.has_value());
    if (!repeated.has_value()) {
        boost::ut::expect(count_code(repeated.error(), rift::SpaceFinalizationErrorCode::inactive_draft) ==
                          std::size_t{1});
        boost::ut::expect(repeated.error().size() == std::size_t{1});
    }
}

void test_empty_regional_state_3d(rift::RiftContext& context)
{
    auto draft = make_built_draft<3>(context, minimal_space_specification());
    if (!draft.has_value()) {
        return;
    }
    const auto result = context.finalize_space(*draft);
    boost::ut::expect(result.has_value());
    if (result.has_value()) {
        boost::ut::expect((*result)->state_layout().regional_entries().empty());
        boost::ut::expect((*result)->state_layout().total_cardinality() == std::uint64_t{8});
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

    [[maybe_unused]] const suite<"SpaceSnapshot"> suite = [] {
        auto& context = *context_ptr;
        "finalization publishes the hand-computed complete 2D layout"_test = [&context] {
            expect(graph_created);
            test_complete_layout(context);
        };
        "snapshot discovery and strong-ID access preserve semantic categories"_test = [&context] {
            test_discovery_and_identity_lookup<2>(context);
        };
        "regional validation is atomic and the active draft can be corrected"_test = [&context] {
            test_regional_validation_retry<2>(context);
        };
        "an unbuilt draft can be built after failed finalization and publication is single-use"_test = [&context] {
            test_unbuilt_retry_and_single_use<2>(context);
        };
        "empty regional state publishes in 3D"_test = [&context] { test_empty_regional_state_3d(context); };
        "3D finalization matches 2D lookup and lifecycle behavior"_test = [&context] {
            test_discovery_and_identity_lookup<3>(context);
            test_regional_validation_retry<3>(context);
            test_unbuilt_retry_and_single_use<3>(context);
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
