// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung

#pragma once

#include <cstddef>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#elif defined(_SC_PAGESIZE)
  #include <unistd.h>
#endif

namespace multi_platforms {

inline std::size_t os_page_size() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return static_cast<std::size_t>(si.dwPageSize ? si.dwPageSize : 4096);
#elif defined(_SC_PAGESIZE)
    long pz = ::sysconf(_SC_PAGESIZE);
    return static_cast<std::size_t>(pz > 0 ? pz : 4096);
#else
    return 4096;
#endif
}

} // namespace multi_platforms
