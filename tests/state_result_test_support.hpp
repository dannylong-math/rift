#pragma once

#include <stdexcept>
#include <utility>

namespace rift::test {

/** Return a required optional/expected value or fail the focused test fixture. */
template<class Result> auto require_state_result(Result&& result)
{
    if (!result) {
        throw std::logic_error("a required state-test operation failed");
    }
    return *std::forward<Result>(result);
}

} // namespace rift::test
