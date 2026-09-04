#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
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

[[nodiscard]] std::vector<rift::PhaseSupportSpecification> empty_support_specifications()
{
    return {{.phase = water_phase(), .requested_cells = {}}, {.phase = air_phase(), .requested_cells = {}}};
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
[[nodiscard]] rift::PhaseSupportSet<dim> make_supports(rift::RiftContext& context,
                                                       const std::shared_ptr<const rift::MeshSnapshot<dim>>& snapshot)
{
    auto result = context.create_phase_supports<dim>(snapshot, empty_support_specifications());
    boost::ut::expect(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] rift::SpaceSpecification phase_ranked_specification()
{
    return {.phase_support_fields =
                {
                    {.phase = water_phase(), .name = "velocity", .component_count = 2, .degree = 2},
                    {.phase = air_phase(), .name = "pressure", .component_count = 1, .degree = 1},
                    {.phase = air_phase(), .name = "velocity", .component_count = 2, .degree = 2},
                },
            .geometry = {
                .continuous_fields =
                    {
                        {.name = "temperature", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                        {.name = "phase-potentials",
                         .components = rift::PhaseBoundFieldComponents{.phases = {water_phase(), air_phase()}},
                         .degree = 2},
                    },
                .discrete_metadata = {}}};
}

[[nodiscard]] rift::SpaceSpecification regional_specification()
{
    return {.phase_support_fields = {},
            .geometry = {.continuous_fields = {{.name = "regional-level-set",
                                                .components = rift::UnboundFieldComponents{.count = 2},
                                                .degree = 1}},
                         .discrete_metadata = {{.name = "phase-label",
                                                .geometry_field_group = "regional-level-set",
                                                .component = 1,
                                                .kind = rift::DiscreteGeometryMetadataKind::phase_label}}}};
}

[[nodiscard]] bool has_code(const rift::SpaceDraftErrors& errors, const rift::SpaceDraftErrorCode code)
{
    return std::ranges::any_of(errors, [code](const auto& error) { return error.code == code; });
}

static_assert(not std::is_copy_constructible_v<rift::SpaceDraft<2>>);
static_assert(not std::is_copy_assignable_v<rift::SpaceDraft<2>>);
static_assert(std::is_nothrow_move_constructible_v<rift::SpaceDraft<2>>);
static_assert(std::is_nothrow_move_assignable_v<rift::SpaceDraft<2>>);
static_assert(not std::is_same_v<rift::PhaseSupportFieldGroupId, rift::GeometryFieldGroupId>);
static_assert(not std::is_same_v<rift::GeometryFieldGroupId, rift::DiscreteGeometryMetadataId>);
static_assert(not std::is_same_v<rift::SpaceEpoch, rift::PhaseSupportSetId>);

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

    [[maybe_unused]] const suite<"SpaceDraft"> suite = [] {
        auto& context = *context_ptr;

        "phase-ranked input becomes a canonical strongly identified schema"_test = [&context] {
            expect(graph_created);
            const auto snapshot = make_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }

            auto supports = make_supports(context, snapshot);
            const auto support_id = supports.id();
            auto result = context.create_space_draft<2>(std::move(supports), phase_ranked_specification());
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            const auto& draft = *result;
            expect(draft.active());
            expect(draft.phase_supports().id() == support_id);
            const auto support_fields = draft.canonical_schema().phase_support_fields();
            expect(support_fields.size() == std::size_t{3});
            if (support_fields.size() == 3) {
                const auto& first = support_fields.front();
                const auto& second = support_fields.subspan(1).front();
                const auto& third = support_fields.back();
                expect(first.id.value() == 0_u);
                expect(first.phase == air_phase());
                expect(first.name == "pressure");
                expect(second.phase == air_phase());
                expect(second.name == "velocity");
                expect(third.phase == water_phase());
                expect(third.component_count == 2_u);
                expect(third.degree == 2_u);
            }

            const auto geometry_fields = draft.canonical_schema().geometry_fields();
            expect(geometry_fields.size() == std::size_t{2});
            if (geometry_fields.size() == 2) {
                const auto& first = geometry_fields.front();
                const auto& second = geometry_fields.back();
                expect(first.id.value() == 0_u);
                expect(first.name == "phase-potentials");
                const auto& phases = std::get<rift::PhaseBoundFieldComponents>(first.components).phases;
                expect(phases == std::vector{air_phase(), water_phase()});
                expect(second.name == "temperature");
                expect(std::get<rift::UnboundFieldComponents>(second.components).count == 1_u);
            }
            expect(draft.canonical_schema().discrete_geometry_metadata().empty());
        };

        "phase-label metadata resolves an unbound geometry component"_test = [&context] {
            const auto snapshot = make_snapshot<3>(context);
            if (snapshot == nullptr) {
                return;
            }
            auto result = context.create_space_draft<3>(make_supports(context, snapshot), regional_specification());
            expect(result.has_value());
            if (!result.has_value()) {
                return;
            }

            auto draft = std::move(result.value());
            expect(draft.active());
            expect(not result->active());
            expect(draft.phase_supports().active());
            expect(draft.canonical_schema().phase_support_fields().empty());
            const auto original_epoch = draft.epoch();
            const auto geometry = draft.canonical_schema().geometry_fields();
            const auto metadata = draft.canonical_schema().discrete_geometry_metadata();
            expect(geometry.size() == std::size_t{1});
            expect(metadata.size() == std::size_t{1});
            if (geometry.size() == 1 && metadata.size() == 1) {
                const auto& item = metadata.front();
                expect(item.id.value() == 0_u);
                expect(item.name == "phase-label");
                expect(item.geometry_field_group == geometry.front().id);
                expect(item.component == 1_u);
                expect(item.kind == rift::DiscreteGeometryMetadataKind::phase_label);
            }

            auto replacement =
                context.create_space_draft<3>(make_supports(context, snapshot), regional_specification());
            expect(replacement.has_value());
            if (replacement.has_value()) {
                draft = std::move(replacement.value());
                expect(draft.active());
                expect(not replacement->active());
                expect(draft.epoch() != original_epoch);
            }
            auto* const self = &draft;
            draft = std::move(*self);
            expect(draft.active());

            auto invalid_specification = regional_specification();
            invalid_specification.geometry.continuous_fields.front().degree = 0;
            const auto invalid =
                context.create_space_draft<3>(make_supports(context, snapshot), std::move(invalid_specification));
            expect(not invalid.has_value());
        };

        "field validation returns typed subjects for names shapes and phases"_test = [&context] {
            const auto snapshot = make_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            std::string invalid_utf8(1, static_cast<char>(0xFF));
            rift::SpaceSpecification specification{
                .phase_support_fields =
                    {
                        {.phase = air_phase(), .name = "", .component_count = 0, .degree = 0},
                        {.phase = air_phase(), .name = "same", .component_count = 1, .degree = 1},
                        {.phase = air_phase(), .name = "same", .component_count = 1, .degree = 1},
                        {.phase = rift::PhaseId::from_index(99), .name = "unknown", .component_count = 1, .degree = 1},
                    },
                .geometry = {
                    .continuous_fields =
                        {
                            {.name = invalid_utf8, .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                            {.name = "bound",
                             .components = rift::PhaseBoundFieldComponents{.phases = {air_phase(), air_phase(),
                                                                                      rift::PhaseId::from_index(99)}},
                             .degree = 1},
                            {.name = "duplicate", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                            {.name = "duplicate", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                            {.name = "empty-bound",
                             .components = rift::PhaseBoundFieldComponents{.phases = {}},
                             .degree = 1},
                            {.name = "zero", .components = rift::UnboundFieldComponents{.count = 0}, .degree = 0},
                        },
                    .discrete_metadata = {}}};

            const auto result =
                context.create_space_draft<2>(make_supports(context, snapshot), std::move(specification));
            expect(not result.has_value());
            if (result.has_value()) {
                return;
            }
            const auto& errors = result.error();
            expect(has_code(errors, rift::SpaceDraftErrorCode::empty_name));
            expect(has_code(errors, rift::SpaceDraftErrorCode::invalid_name_encoding));
            expect(has_code(errors, rift::SpaceDraftErrorCode::duplicate_name));
            expect(has_code(errors, rift::SpaceDraftErrorCode::unknown_phase));
            expect(has_code(errors, rift::SpaceDraftErrorCode::zero_component_count));
            expect(has_code(errors, rift::SpaceDraftErrorCode::invalid_polynomial_degree));
            expect(has_code(errors, rift::SpaceDraftErrorCode::missing_phase_binding));
            expect(has_code(errors, rift::SpaceDraftErrorCode::duplicate_phase_binding));
            expect(has_code(errors, rift::SpaceDraftErrorCode::unknown_phase_binding));
            expect(std::ranges::all_of(errors, [](const auto& error) { return error.rank == 0_u; }));
            expect(std::ranges::any_of(errors, [](const auto& error) {
                return std::holds_alternative<rift::PhaseSupportFieldGroupErrorSubject>(error.subject);
            }));
            expect(std::ranges::any_of(errors, [](const auto& error) {
                return std::holds_alternative<rift::GeometryFieldGroupErrorSubject>(error.subject);
            }));
        };

        "metadata validation reports every target and kind rule"_test = [&context] {
            const auto snapshot = make_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            rift::SpaceSpecification specification{
                .phase_support_fields = {},
                .geometry = {
                    .continuous_fields =
                        {
                            {.name = "bound",
                             .components = rift::PhaseBoundFieldComponents{.phases = {air_phase(), water_phase()}},
                             .degree = 1},
                            {.name = "ambiguous", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                            {.name = "ambiguous", .components = rift::UnboundFieldComponents{.count = 1}, .degree = 1},
                            {.name = "unbound", .components = rift::UnboundFieldComponents{.count = 2}, .degree = 1},
                        },
                    .discrete_metadata = {
                        {.name = "bound-label",
                         .geometry_field_group = "bound",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "ambiguous-target",
                         .geometry_field_group = "ambiguous",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "duplicate-name",
                         .geometry_field_group = "unbound",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "duplicate-name",
                         .geometry_field_group = "unbound",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "missing",
                         .geometry_field_group = "absent",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "out-of-range",
                         .geometry_field_group = "unbound",
                         .component = 4,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "other-component",
                         .geometry_field_group = "unbound",
                         .component = 1,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                        {.name = "unsupported",
                         .geometry_field_group = "unbound",
                         .component = 0,
                         .kind = static_cast<rift::DiscreteGeometryMetadataKind>(99)},
                        {.name = "unknown-after-all-fields",
                         .geometry_field_group = "zzz",
                         .component = 0,
                         .kind = rift::DiscreteGeometryMetadataKind::phase_label},
                    }}};

            const auto result =
                context.create_space_draft<2>(make_supports(context, snapshot), std::move(specification));
            expect(not result.has_value());
            if (result.has_value()) {
                return;
            }
            const auto& errors = result.error();
            expect(has_code(errors, rift::SpaceDraftErrorCode::duplicate_name));
            expect(has_code(errors, rift::SpaceDraftErrorCode::unknown_metadata_field_group));
            expect(has_code(errors, rift::SpaceDraftErrorCode::metadata_component_out_of_range));
            expect(has_code(errors, rift::SpaceDraftErrorCode::metadata_requires_unbound_components));
            expect(has_code(errors, rift::SpaceDraftErrorCode::unsupported_metadata_kind));
            expect(has_code(errors, rift::SpaceDraftErrorCode::duplicate_metadata_target));
            expect(std::ranges::any_of(errors, [](const auto& error) {
                return std::holds_alternative<rift::DiscreteGeometryMetadataErrorSubject>(error.subject);
            }));
        };

        "failed construction consumes no epoch and moved-from support is rejected"_test = [&context] {
            const auto snapshot = make_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            auto first = context.create_space_draft<2>(make_supports(context, snapshot), regional_specification());
            expect(first.has_value());

            auto supports = make_supports(context, snapshot);
            auto active_supports = std::move(supports);
            expect(not supports.active());
            // Intentionally exercise the public moved-from validation path.
            const auto inactive = context.create_space_draft<2>( // NOLINT(bugprone-use-after-move)
                std::move(supports), regional_specification());
            expect(not inactive.has_value());
            if (!inactive.has_value()) {
                expect(has_code(inactive.error(), rift::SpaceDraftErrorCode::inactive_phase_support_set));
            }

            auto second = context.create_space_draft<2>(std::move(active_supports), regional_specification());
            expect(second.has_value());
            if (first.has_value() && second.has_value()) {
                expect(second->epoch().value() == first->epoch().value() + std::uint64_t{1});
            }
        };

        "moving a draft transfers active ownership"_test = [&context] {
            const auto snapshot = make_snapshot<2>(context);
            if (snapshot == nullptr) {
                return;
            }
            auto first_result =
                context.create_space_draft<2>(make_supports(context, snapshot), phase_ranked_specification());
            auto second_result =
                context.create_space_draft<2>(make_supports(context, snapshot), regional_specification());
            expect(first_result.has_value() && second_result.has_value());
            if (!first_result.has_value() || !second_result.has_value()) {
                return;
            }

            auto moved = std::move(first_result.value());
            expect(moved.active());
            expect(not first_result->active());
            const auto moved_epoch = moved.epoch();
            moved = std::move(second_result.value());
            expect(moved.active());
            expect(not second_result->active());
            expect(moved.epoch() != moved_epoch);
            auto* const self = &moved;
            moved = std::move(*self);
            expect(moved.active());
        };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
