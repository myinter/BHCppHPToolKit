// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung

#pragma once

#include <cstddef>
#include <cstdint>

namespace foudations {

inline std::size_t gcd(std::size_t a, std::size_t b) noexcept {
    while (b != 0) {
        const std::size_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

inline std::size_t lcm(std::size_t a, std::size_t b) noexcept {
    return (a / gcd(a, b)) * b;
}

inline std::size_t round_up(std::size_t value, std::size_t align) noexcept {
    if (align == 0) {
        return value;
    }
    const std::size_t remainder = value % align;
    return remainder == 0 ? value : (value + (align - remainder));
}

inline bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

inline std::size_t round_up_power_of_two(std::size_t value) noexcept {
    if (value <= 1) {
        return 1;
    }

    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    if (sizeof(std::size_t) >= 8) {
        value |= value >> 32;
    }
    return value + 1;
}

inline std::size_t round_up_to_multiple(std::size_t value, std::size_t multiple) noexcept {
    if (multiple == 0) {
        return value;
    }
    const std::size_t remainder = value % multiple;
    return remainder == 0 ? value : (value + (multiple - remainder));
}

inline std::size_t find_first_zero_bit(uint64_t value) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    return static_cast<std::size_t>(__builtin_ctzll(~value));
#else
    std::size_t index = 0;
    while ((value & (uint64_t(1) << index)) != 0) {
        ++index;
    }
    return index;
#endif
}

} // namespace foudations
