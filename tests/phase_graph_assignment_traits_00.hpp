#pragma once

#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <type_traits>

static_assert(std::is_copy_constructible_v<rift::PhaseGraph>);
static_assert(std::is_move_constructible_v<rift::PhaseGraph>);
static_assert(!std::is_copy_assignable_v<rift::PhaseGraph>);
static_assert(!std::is_move_assignable_v<rift::PhaseGraph>);
static_assert(std::is_copy_constructible_v<rift::RunConfiguration>);
static_assert(std::is_copy_assignable_v<rift::RunConfiguration>);
static_assert(std::is_move_constructible_v<rift::RunConfiguration>);
static_assert(std::is_move_assignable_v<rift::RunConfiguration>);

namespace rift_test::phase_graph_assignment_traits_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "phase graph and run configuration expose their intended assignment traits"_test = [] {
        expect(std::is_copy_constructible_v<rift::PhaseGraph>);
        expect(std::is_move_constructible_v<rift::PhaseGraph>);
        expect(!std::is_copy_assignable_v<rift::PhaseGraph>);
        expect(!std::is_move_assignable_v<rift::PhaseGraph>);
        expect(std::is_copy_constructible_v<rift::RunConfiguration>);
        expect(std::is_copy_assignable_v<rift::RunConfiguration>);
        expect(std::is_move_constructible_v<rift::RunConfiguration>);
        expect(std::is_move_assignable_v<rift::RunConfiguration>);
    };
}

} // namespace rift_test::phase_graph_assignment_traits_00
