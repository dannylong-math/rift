#pragma once

#include <array>
#include <boost/ut.hpp>
#include <rift/discrete_state.hpp>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rift_test::state_transition_error_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "state transition errors are fixed-size allocation-free values"_test = [] {
        static_assert(std::is_trivially_copyable_v<rift::StateTransitionError>);
        static_assert(sizeof(rift::StateTransitionError) == sizeof(rift::StateTransitionErrorCode));
        constexpr auto code = rift::StateTransitionErrorCode::unknown_snapshot;
        const rift::StateTransitionError error{code};
        expect(error.message() == std::string_view{"the state snapshot is not retained by this store"});
        expect(error == rift::StateTransitionError{code});
    };
    "every state transition error code has stable allocation-free text"_test = [] {
        constexpr std::array cases{
            std::pair{rift::StateTransitionErrorCode::operation_mismatch,
                      std::string_view{"ranks entered different state operations"}},
            std::pair{rift::StateTransitionErrorCode::store_mismatch,
                      std::string_view{"ranks supplied different state stores"}},
            std::pair{rift::StateTransitionErrorCode::transaction_mismatch,
                      std::string_view{"ranks supplied different state transactions"}},
            std::pair{rift::StateTransitionErrorCode::argument_mismatch,
                      std::string_view{"ranks supplied different state arguments"}},
            std::pair{rift::StateTransitionErrorCode::replicated_layout_mismatch,
                      std::string_view{"the replicated state layout differs between ranks"}},
            std::pair{rift::StateTransitionErrorCode::retention_policy_mismatch,
                      std::string_view{"the replicated state retention policy differs between ranks"}},
            std::pair{rift::StateTransitionErrorCode::expired_store,
                      std::string_view{"the state store authority has expired"}},
            std::pair{rift::StateTransitionErrorCode::inactive_transaction,
                      std::string_view{"the state transaction is inactive"}},
            std::pair{rift::StateTransitionErrorCode::unknown_snapshot,
                      std::string_view{"the state snapshot is not retained by this store"}},
            std::pair{rift::StateTransitionErrorCode::wrong_candidate_state,
                      std::string_view{"only a private state candidate can be published"}},
            std::pair{rift::StateTransitionErrorCode::invalid_discard,
                      std::string_view{"only a retained private state candidate can be discarded"}},
            std::pair{rift::StateTransitionErrorCode::pin_limit_reached,
                      std::string_view{"the private snapshot pin limit has been reached"}},
            std::pair{rift::StateTransitionErrorCode::stale_accepted_root,
                      std::string_view{"the private candidate descends from a stale accepted root"}},
            std::pair{rift::StateTransitionErrorCode::identity_exhausted,
                      std::string_view{"the finite collective state identity sequence is exhausted"}},
            std::pair{rift::StateTransitionErrorCode::field_reference_mismatch,
                      std::string_view{"the field reference belongs to another space or phase"}},
            std::pair{rift::StateTransitionErrorCode::unknown_field,
                      std::string_view{"the field identity does not exist in this state layout"}},
            std::pair{rift::StateTransitionErrorCode::unknown_regional_entry,
                      std::string_view{"the regional scalar identity does not exist in this state layout"}},
        };
        for (const auto& [code, message] : cases) {
            expect(rift::StateTransitionError{code}.message() == message);
        }
    };
}

} // namespace rift_test::state_transition_error_00
