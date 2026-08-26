#pragma once

#include <array>
#include <iostream>
#include <span>
#include <string_view>

namespace rift_test {

[[nodiscard]] inline std::string_view test_mode(const int argc, char** argv)
{
    if (argc != 2 || argv == nullptr) {
        return {};
    }
    const std::span<char*> arguments{argv, static_cast<std::size_t>(argc)};
    const auto* const mode = arguments.back();
    if (mode == nullptr) {
        return {};
    }
    return mode;
}

inline int invalid_test_mode(const std::string_view runner, const std::string_view mode)
{
    std::cerr << runner << " requires one reviewed test mode; received '" << mode << "'\n";
    return 2;
}

template<bool reverse, std::size_t size> void register_all(const std::array<void (*)(), size>& registrations)
{
    if constexpr (reverse) {
        for (auto registration = registrations.rbegin(); registration != registrations.rend(); ++registration) {
            (*registration)();
        }
    }
    else {
        for (const auto registration : registrations) {
            registration();
        }
    }
}

} // namespace rift_test
