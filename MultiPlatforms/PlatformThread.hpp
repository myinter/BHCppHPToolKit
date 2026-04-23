// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung

#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <sys/sysctl.h>
#endif

namespace multi_platforms {

enum class ThreadPriority {
    UI,
    High,
    Normal,
    Background
};

// CPU类型枚举 / CPU type enumeration
enum class CPUCoreType {
    Unknown, // 未知 Unknown
    Big, // 大核心 Big
    Medium, // 中核 Medium
    Little, // 小核 Little
    HyperThread // 超线程逻辑核 Hyper-thread
};

// CPU类型信息结构体 / CPU type information struct
struct CPUCoreInfo {
    unsigned int index;
    CPUCoreType type;
};

namespace detail {

inline bool readTextFile(const std::string& path, std::string& output) {
    std::ifstream file(path.c_str(), std::ios::in);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }

    output = line;
    return true;
}

inline bool readUnsignedIntFile(const std::string& path, unsigned int& value) {
    std::ifstream file(path.c_str(), std::ios::in);
    if (!file.is_open()) {
        return false;
    }

    file >> value;
    return !file.fail();
}

inline std::vector<unsigned int> parseCPUIndexList(const std::string& text) {
    std::vector<unsigned int> result;
    std::size_t start = 0;

    while (start < text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }

        const std::string token = text.substr(start, end - start);
        const std::size_t dashPos = token.find('-');
        if (dashPos == std::string::npos) {
            if (!token.empty()) {
                result.push_back(static_cast<unsigned int>(std::strtoul(token.c_str(), NULL, 10)));
            }
        } else {
            const unsigned int rangeStart = static_cast<unsigned int>(std::strtoul(token.substr(0, dashPos).c_str(), NULL, 10));
            const unsigned int rangeEnd = static_cast<unsigned int>(std::strtoul(token.substr(dashPos + 1).c_str(), NULL, 10));
            for (unsigned int value = rangeStart; value <= rangeEnd; ++value) {
                result.push_back(value);
            }
        }

        start = end + 1;
    }

    return result;
}

inline CPUCoreType classifyTierByRank(std::size_t rank, std::size_t tierCount) {
    if (tierCount <= 1) {
        return CPUCoreType::Unknown;
    }
    if (tierCount == 2) {
        return rank == 0 ? CPUCoreType::Big : CPUCoreType::Little;
    }
    if (rank == 0) {
        return CPUCoreType::Big;
    }
    if (rank + 1 == tierCount) {
        return CPUCoreType::Little;
    }
    return CPUCoreType::Medium;
}

#ifdef __APPLE__
inline bool sysctlUnsignedInt(const char* name, unsigned int& value) {
    size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, NULL, 0) == 0;
}
#endif

// 收集CPU核心信息列表 / Collect CPU core information list
std::vector<CPUCoreInfo> collectCPUCoreInfos() {
    std::vector<CPUCoreInfo> infos;

#ifdef _WIN32
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    const unsigned int logicalCount = static_cast<unsigned int>(systemInfo.dwNumberOfProcessors);
    infos.resize(logicalCount);
    for (unsigned int i = 0; i < logicalCount; ++i) {
        infos[i].index = i;
        infos[i].type = CPUCoreType::Unknown;
    }

    DWORD bufferSize = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &bufferSize);
    if (bufferSize == 0) {
        return infos;
    }

    std::vector<unsigned char> buffer(bufferSize);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX processorInfo =
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(&buffer[0]);

    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, processorInfo, &bufferSize)) {
        return infos;
    }

    unsigned int processed = 0;
    while (processed < bufferSize) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX current =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(&buffer[processed]);

        if (current->Relationship == RelationProcessorCore) {
            const bool hasSMT = (current->Processor.Flags & LTP_PC_SMT) != 0;
            DWORD_PTR mask = current->Processor.GroupMask[0].Mask;
            bool firstLogical = true;

            for (unsigned int bit = 0; bit < sizeof(DWORD_PTR) * 8 && bit < logicalCount; ++bit) {
                if ((mask & (static_cast<DWORD_PTR>(1) << bit)) == 0) {
                    continue;
                }
                infos[bit].type = (hasSMT && !firstLogical) ? CPUCoreType::HyperThread : CPUCoreType::Unknown;
                firstLogical = false;
            }
        }

        processed += current->Size;
    }

#elif defined(__linux__)
    unsigned int logicalCount = static_cast<unsigned int>(std::thread::hardware_concurrency());
    if (logicalCount == 0) {
        long detected = sysconf(_SC_NPROCESSORS_ONLN);
        logicalCount = detected > 0 ? static_cast<unsigned int>(detected) : 1;
    }

    infos.resize(logicalCount);
    std::vector<unsigned int> maxFreq(logicalCount, 0);
    std::vector<bool> primaryThread(logicalCount, true);

    for (unsigned int cpu = 0; cpu < logicalCount; ++cpu) {
        infos[cpu].index = cpu;
        infos[cpu].type = CPUCoreType::Unknown;

        std::string siblingsPath = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list";
        std::string siblingsText;
        if (readTextFile(siblingsPath, siblingsText)) {
            std::vector<unsigned int> siblings = parseCPUIndexList(siblingsText);
            if (!siblings.empty()) {
                const unsigned int leader = *std::min_element(siblings.begin(), siblings.end());
                if (cpu != leader) {
                    primaryThread[cpu] = false;
                    infos[cpu].type = CPUCoreType::HyperThread;
                }
            }
        }

        std::string freqPath1 = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/cpuinfo_max_freq";
        std::string freqPath2 = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_max_freq";
        if (!readUnsignedIntFile(freqPath1, maxFreq[cpu])) {
            readUnsignedIntFile(freqPath2, maxFreq[cpu]);
        }
    }

    std::set<unsigned int, std::greater<unsigned int> > tiers;
    for (unsigned int cpu = 0; cpu < logicalCount; ++cpu) {
        if (primaryThread[cpu] && maxFreq[cpu] > 0) {
            tiers.insert(maxFreq[cpu]);
        }
    }

    std::map<unsigned int, CPUCoreType> tierTypes;
    std::size_t rank = 0;
    for (std::set<unsigned int, std::greater<unsigned int> >::const_iterator it = tiers.begin(); it != tiers.end(); ++it, ++rank) {
        tierTypes[*it] = classifyTierByRank(rank, tiers.size());
    }

    for (unsigned int cpu = 0; cpu < logicalCount; ++cpu) {
        if (!primaryThread[cpu]) {
            continue;
        }
        std::map<unsigned int, CPUCoreType>::const_iterator found = tierTypes.find(maxFreq[cpu]);
        if (found != tierTypes.end()) {
            infos[cpu].type = found->second;
        }
    }

#elif defined(__APPLE__)
    unsigned int logicalCount = 0;
    if (!sysctlUnsignedInt("hw.logicalcpu", logicalCount)) {
        logicalCount = static_cast<unsigned int>(std::thread::hardware_concurrency());
    }
    if (logicalCount == 0) {
        logicalCount = 1;
    }

    infos.resize(logicalCount);
    for (unsigned int i = 0; i < logicalCount; ++i) {
        infos[i].index = i;
        infos[i].type = CPUCoreType::Unknown;
    }

    unsigned int perfLevelCount = 0;
    if (sysctlUnsignedInt("hw.nperflevels", perfLevelCount) && perfLevelCount > 1) {
        unsigned int offset = 0;
        for (unsigned int level = 0; level < perfLevelCount && offset < logicalCount; ++level) {
            unsigned int levelCPUs = 0;
            std::string key = "hw.perflevel" + std::to_string(level) + ".logicalcpu";
            if (!sysctlUnsignedInt(key.c_str(), levelCPUs)) {
                key = "hw.perflevel" + std::to_string(level) + ".logicalcpumax";
                if (!sysctlUnsignedInt(key.c_str(), levelCPUs)) {
                    continue;
                }
            }

            CPUCoreType type = classifyTierByRank(level, perfLevelCount);
            for (unsigned int i = 0; i < levelCPUs && offset < logicalCount; ++i, ++offset) {
                infos[offset].type = type;
            }
        }
    } else {
        unsigned int physicalCount = 0;
        if (sysctlUnsignedInt("hw.physicalcpu", physicalCount) && physicalCount > 0 && logicalCount > physicalCount) {
            for (unsigned int i = physicalCount; i < logicalCount; ++i) {
                infos[i].type = CPUCoreType::HyperThread;
            }
        }
    }

#else
    unsigned int logicalCount = static_cast<unsigned int>(std::thread::hardware_concurrency());
    if (logicalCount == 0) {
        logicalCount = 1;
    }
    infos.resize(logicalCount);
    for (unsigned int i = 0; i < logicalCount; ++i) {
        infos[i].index = i;
        infos[i].type = CPUCoreType::Unknown;
    }
#endif

    return infos;
}

} // namespace detail

inline void setThreadPriority(std::thread &t, ThreadPriority p) {
#ifdef _WIN32
    int pri = THREAD_PRIORITY_NORMAL;
    switch (p) {
        case ThreadPriority::UI: pri = THREAD_PRIORITY_TIME_CRITICAL; break;
        case ThreadPriority::High: pri = THREAD_PRIORITY_HIGHEST; break;
        case ThreadPriority::Normal: pri = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::Background: pri = THREAD_PRIORITY_LOWEST; break;
    }
    SetThreadPriority(t.native_handle(), pri);
#elif defined(__linux__) || defined(__APPLE__)
    sched_param sch;
    int policy;
    pthread_getschedparam(pthread_self(), &policy, &sch);
    switch (p) {
        case ThreadPriority::UI: sch.sched_priority = sched_get_priority_max(SCHED_FIFO); break;
        case ThreadPriority::High: sch.sched_priority = (sched_get_priority_max(SCHED_FIFO) * 3) / 4; break;
        case ThreadPriority::Normal: sch.sched_priority = 0; break;
        case ThreadPriority::Background: sch.sched_priority = sched_get_priority_min(SCHED_FIFO); break;
    }
    pthread_setschedparam(t.native_handle(), SCHED_FIFO, &sch);
#else
    (void)t;
    (void)p;
#endif
}

// 将某个线程绑定到某个CPU核心 / Bind a thread to a specific CPU core
inline bool bindThreadToCPU(std::thread &t, unsigned int cpuIndex) {
#ifdef _WIN32
    DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cpuIndex;
    return SetThreadAffinityMask(t.native_handle(), mask) != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuIndex, &cpuset);
    return pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(__APPLE__)
    thread_port_t machThreadPort = pthread_mach_thread_np(t.native_handle());
    thread_affinity_policy_data_t policy;
    policy.affinity_tag = static_cast<integer_t>(cpuIndex + 1);
    return thread_policy_set(machThreadPort,
                             THREAD_AFFINITY_POLICY,
                             reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS;
#else
    (void)t;
    (void)cpuIndex;
    return false;
#endif
}

// 将当前线程绑定到某个CPU核心 / Bind the current thread to a specific CPU core
inline bool bindCurrentThreadToCPU(unsigned int cpuIndex) {
#ifdef _WIN32
    DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cpuIndex;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuIndex, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(__APPLE__)
    thread_port_t machThreadPort = pthread_mach_thread_np(pthread_self());
    thread_affinity_policy_data_t policy;
    policy.affinity_tag = static_cast<integer_t>(cpuIndex + 1);
    return thread_policy_set(machThreadPort,
                             THREAD_AFFINITY_POLICY,
                             reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS;
#else
    (void)cpuIndex;
    return false;
#endif
}

// 提供懒加载缓存的CPU核心信息列表 / Provide a lazily loaded cached list of CPU core information
inline const std::vector<CPUCoreInfo>& detectCPUCoreInfos() {
    static const std::vector<CPUCoreInfo> infos = detail::collectCPUCoreInfos();
    return infos;
}

inline const std::vector<CPUCoreInfo>& getCPUCoreInfos() {
    return detectCPUCoreInfos();
}

inline unsigned int getCPUCount() {
    return static_cast<unsigned int>(getCPUCoreInfos().size());
}

inline CPUCoreType getCPUCoreType(unsigned int cpuIndex) {
    const std::vector<CPUCoreInfo>& infos = getCPUCoreInfos();
    if (cpuIndex >= infos.size()) {
        return CPUCoreType::Unknown;
    }
    return infos[cpuIndex].type;
}

inline const CPUCoreInfo* getCPUCoreInfo(unsigned int cpuIndex) {
    const std::vector<CPUCoreInfo>& infos = getCPUCoreInfos();
    if (cpuIndex >= infos.size()) {
        return nullptr;
    }
    return &infos[cpuIndex];
}

} // namespace multi_platforms
