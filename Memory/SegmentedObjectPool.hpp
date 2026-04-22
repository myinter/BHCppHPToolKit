// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * SegmentedObjectPool.hpp
 *
 * SegmentedObjectPool 对象池模板 / Segmented Object Pool Template
 *
 * 功能 / Features:
 * 1. 统一分配对象 / Unified object allocation
 * 2. 避免对象反复申请释放 / Avoid frequent allocation/deallocation
 * 3. 对象在内存中连续地址分配 / Objects allocated in contiguous memory
 * 4. 对象连续分配，极大提高遍历对象过程中的缓存命中率 / Continuous object allocation significantly reduces cache misses during traversal.
 * 5. 使用数组索引代替链表管理可用对象，提高内存局部性 / Use array indices instead of linked lists to manage free objects, improving memory locality.
 * 6. 申请空间大小依据操作系统内存页面大小，并以分段方式动态扩容 / Segment size follows OS page size and grows dynamically by segments.
 * 7. 适用于即时消息、高频交易系统、游戏数据等性能敏感场景 / Suitable for IM, HFT, game data, and other performance-sensitive scenarios.
 * 8. 带有 Atomic APIs，可用于并发环境中的创建和回收 / Provides atomic APIs for concurrent allocation and reclamation.
 */

#pragma once
#include "../Foudations/MathBits.hpp"
#include "../MultiPlatforms/PlatformMemory.hpp"
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>
#include <memory>
#include <type_traits>
#include <cassert>
#include <utility>
#include <iostream>
#include <algorithm>
#include <atomic>
#include <stack>

// ----------------------------
// SegmentedObjectPool 定义 / Definition
// ----------------------------
template <class T>
class SegmentedObjectPool {
    static_assert(!std::is_abstract<T>::value, "T must be a complete, non-abstract type");

    struct Segment {
        unsigned char* data = nullptr;                // 内存块 / Memory block
        std::size_t capacity = 0;                 // 可容纳对象数 / Number of objects
        std::size_t next_uninit = 0;              // 尚未构造的下一个索引 / Next uninitialized index

        Segment() = default;
        Segment(unsigned char* d, std::size_t cap) : data(d), capacity(cap), next_uninit(0) /*free_flags(cap, 0), free_hint(0)*/ {}
    };

    // 用于线程安全场景的自旋锁 Spin lock for thread-safe scenarios
    // 用于确保对象回收的线程安全 Used to ensure thread safety for object recycling
    struct SpinLock {
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
        inline void lock() noexcept {
            while (flag.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
            }
        }
        inline void unlock() noexcept {
            flag.clear(std::memory_order_release);
        }
    };

    // RAII LockGuard for facilitating the use of spin locks
    // 简化自旋锁使用的 RAII LockGuard
    struct LockGuard {
        SpinLock& lock;
        explicit LockGuard(SpinLock& l) : lock(l) { lock.lock(); }
        ~LockGuard() { lock.unlock(); }
    };

    std::stack<T*> free_stack_;   // 空闲对象栈 Free objects stack

public:
    using value_type = T;

    inline static SegmentedObjectPool& instance() {
        static SegmentedObjectPool inst;
        return inst;
    }

    explicit SegmentedObjectPool(std::size_t min_pages_per_segment = 0, double growth = 1.0)
    : page_size_(multi_platforms::os_page_size()),
      slot_size_(foudations::round_up(std::max(sizeof(T), sizeof(void*)), alignof(T))),
      pages_per_segment_base_(compute_min_pages(min_pages_per_segment)),
      growth_factor_(growth > 1.0 ? growth : 1.0) {}

    ~SegmentedObjectPool() { clear(); }
    SegmentedObjectPool(const SegmentedObjectPool&) = delete;
    SegmentedObjectPool& operator=(const SegmentedObjectPool&) = delete;

    // 分配对象 / Allocate object
    template <class... Args>
    T* allocate(Args&&... args) {
        // 1. 优先使用 stack 中的空闲对象
        if (!free_stack_.empty()) {
            T* obj = free_stack_.top();
            free_stack_.pop();
            new (obj) T(std::forward<Args>(args)...);
            obj->mark_in_use();
            ++live_count_;
            return obj;
        }

        // 2. 分配未初始化空间
        if (!segments_.empty()) {
            Segment& seg = segments_.back();
            if (seg.next_uninit < seg.capacity) {
                unsigned char* ptr = seg.data + seg.next_uninit * slot_size_;
                ++seg.next_uninit;
                T* obj = reinterpret_cast<T*>(ptr);
                new (obj) T(std::forward<Args>(args)...);
                obj->mark_in_use();
                ++live_count_;
                return obj;
            }
        }

        // 3. 扩容新段
        add_segment_();
        Segment& seg = segments_.back();
        unsigned char* ptr = seg.data + seg.next_uninit * slot_size_;
        ++seg.next_uninit;
        T* obj = reinterpret_cast<T*>(ptr);
        new (obj) T(std::forward<Args>(args)...);
        obj->mark_in_use();
        ++live_count_;
        return obj;
    }

    // 回收对象 / Deallocate object
    void deallocate(T* p) noexcept {
        
        if (!p) return;
        p->~T();
        free_stack_.push(p);  // 直接压入 stack
        --live_count_;

    }

    // =============================================================
    // 🔒 线程安全 API（池内部同步）
    // Thread-safe API (internal synchronization within the pool)
    // =============================================================
    template <class... Args>
    T* atomic_allocate(Args&&... args) {
        LockGuard g(lock_);
        return allocate(std::forward<Args>(args)...);
    }

    void atomic_deallocate(T* p) noexcept {
        if (!p) return;
        LockGuard g(lock_);
        deallocate(p);
    }

    void atomic_clear() noexcept {
        LockGuard g(lock_);
        clear();
    }

    // 清空池子 / Clear all memory
    void clear() noexcept {
        for (auto& seg : segments_) {
            ::operator delete(seg.data);
            seg.data = nullptr;
        }
        segments_.clear();
        live_count_ = 0;
        next_pages_hint_ = pages_per_segment_base_;
    }

    std::size_t live() const noexcept { return live_count_; }
    std::size_t segments() const noexcept { return segments_.size(); }
    std::size_t capacity_total() const noexcept {
        std::size_t c = 0; for (auto const& s : segments_) c += s.capacity; return c; }

private:
    
    // 分配和回收操作的具体实现
    // The specific implementation of allocation and recycling operations

    std::size_t compute_min_pages(std::size_t user_min_pages) const noexcept {
        const std::size_t ps = page_size_;
        const std::size_t ss = slot_size_;
        const std::size_t l = foudations::lcm(ps, ss);
        std::size_t min_pages = l / ps;
        if (user_min_pages > 0) {
            std::size_t k = (user_min_pages + min_pages - 1) / min_pages;
            min_pages *= k;
        }
        return min_pages;
    }

    void add_segment_() {
        if (segments_.empty()) {
            next_pages_hint_ = pages_per_segment_base_;
        } else {
            double target = static_cast<double>(next_pages_hint_) * growth_factor_;
            std::size_t pages = static_cast<std::size_t>(target);
            if (pages < next_pages_hint_ + pages_per_segment_base_)
                pages = next_pages_hint_ + pages_per_segment_base_;
            std::size_t rem = pages % pages_per_segment_base_;
            if (rem) pages += (pages_per_segment_base_ - rem);
            next_pages_hint_ = pages;
        }
        const std::size_t seg_bytes = next_pages_hint_ * page_size_;
        const std::size_t capacity = seg_bytes / slot_size_;
        unsigned char* raw = static_cast<unsigned char*>(::operator new(seg_bytes));
        segments_.emplace_back(raw, capacity);
    }

private:
    std::vector<Segment> segments_;
    std::size_t page_size_ = multi_platforms::os_page_size();
    std::size_t slot_size_ = 0;
    std::size_t pages_per_segment_base_ = 0;
    double growth_factor_ = 1.0;
    std::size_t next_pages_hint_ = 0;
    std::size_t live_count_ = 0;

    // Thread-safe lock
    SpinLock lock_;
};

// ----------------------------
// PooledObject 基类 / Base class for pooled objects
// ----------------------------
template <class Derived>
struct PooledObject {
    virtual ~PooledObject() = default;
    virtual void reset() {}

    // 用于极致性能场景的线程不安全创建方法 / Thread-unsafe creation method for extreme performance scenarios
    template <class... Args>
    static Derived* create(Args&&... args) {
        return SegmentedObjectPool<Derived>::instance().allocate(std::forward<Args>(args)...);
    }

    // 线程安全版本的创建方法 Thread-safe version of the create method
    template <class... Args>
    static Derived* atomic_create(Args&&... args) {
        return SegmentedObjectPool<Derived>::instance().atomic_allocate(std::forward<Args>(args)...);
    }

    // 用于极致性能场景的线程不安全回收方法 / Thread-unsafe recycle method for extreme performance scenarios
    inline void recycle() {
        this->reset();
        recycled_ = true;
        SegmentedObjectPool<Derived>::instance().deallocate(static_cast<Derived*>(this));
    }

    // 线程安全版本的回收方法 Thread-safe version of the recycling method
    inline void atomic_recycle() {
        this->reset();
        recycled_ = true;
        SegmentedObjectPool<Derived>::instance().atomic_deallocate(static_cast<Derived*>(this));
    }

    inline bool is_recycled() const noexcept { return recycled_; }
    inline void mark_in_use() noexcept { recycled_ = false; }

private:
    bool recycled_ = false;
};
