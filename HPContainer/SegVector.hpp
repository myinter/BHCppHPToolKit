// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * SegVector.hpp
 *
 * SegmentVector - 分段向量容器
 *
 * 功能 / Features:
 * 1. 采用按段分配的连续块结构管理元素 / Manages elements using segmented contiguous blocks.
 * 2. 根据系统页面大小推导默认段大小 / Derives default segment size from the system page size.
 * 3. 支持写时拷贝控制块 / Supports copy-on-write control blocks.
 * 4. 减少大块连续扩容带来的搬迁成本 / Reduces relocation cost compared with monolithic contiguous growth.
 * 5. 适用于需要较大容量且关注扩容性能的场景 / Suitable for large-capacity scenarios where growth cost matters.
 */

#pragma once
#include "../MultiPlatforms/PlatformMemory.hpp"
#include <cassert>
#include <algorithm>
#include <stack>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <iostream>
#include <atomic>

// 获取系统页面大小 / Get system page size
inline size_t getPageSize() {
    return multi_platforms::os_page_size();
}

// 计算page size对应的shift值 / calculate shift from page size
inline size_t getSegmentShift(size_t segmentSize) {
    size_t shift = 0;
    while ((1ULL << shift) < segmentSize) ++shift;
    return shift;
}

inline size_t getSegmentMask(size_t segmentSize) {
    return segmentSize - 1;
}

template<typename T>
class SegmentVector {
private:
    struct ControlBlock {
        T** segments;
        size_t size;
        size_t segmentCount;
        size_t segmentCapacity;
        size_t segmentSize;
        size_t capacity;   // 缓存容量，避免每次乘法 / cached capacity
        std::atomic<size_t> refCount;
    };

    ControlBlock* ctrl;
    static std::stack<T*> _freeSegments;

    static T** allocateSegmentArray(size_t count) {
        return new T*[count];
    }

    static T* allocateSegment(size_t segmentSize) {
        if (!_freeSegments.empty()) {
            T* seg = _freeSegments.top();
            _freeSegments.pop();
            return seg;
        }
        return new T[segmentSize];
    }

    static void recycleSegment(T* seg) {
        _freeSegments.push(seg);
    }

    static void freeControlBlock(ControlBlock* c) {
        for (size_t i = 0; i < c->segmentCount; ++i) {
            recycleSegment(c->segments[i]);
        }
        delete[] c->segments;
        delete c;
    }

    ControlBlock* deepCopy(ControlBlock* other) {
        ControlBlock* c = new ControlBlock();
        c->size = other->size;
        c->segmentCount = other->segmentCount;
        c->segmentCapacity = other->segmentCapacity;
        c->segmentSize = other->segmentSize;
        c->capacity = other->capacity;
        c->refCount = 1;

        c->segments = allocateSegmentArray(c->segmentCapacity);
        for (size_t i = 0; i < c->segmentCount; ++i) {
            c->segments[i] = allocateSegment(c->segmentSize);
            std::copy(other->segments[i], other->segments[i] + c->segmentSize, c->segments[i]);
        }
        return c;
    }

    void detach() {
        if (ctrl && ctrl->refCount > 1) {
            ControlBlock* newCtrl = deepCopy(ctrl);
            --ctrl->refCount;
            ctrl = newCtrl;
        }
    }

    void addSegment() {
        detach();
        if (ctrl->segmentCount == ctrl->segmentCapacity) {
            size_t newCapacity = ctrl->segmentCapacity * 2;
            T** newSegments = allocateSegmentArray(newCapacity);
            for (size_t i = 0; i < ctrl->segmentCount; ++i) {
                newSegments[i] = ctrl->segments[i];
            }
            delete[] ctrl->segments;
            ctrl->segments = newSegments;
            ctrl->segmentCapacity = newCapacity;
        }
        ctrl->segments[ctrl->segmentCount++] = allocateSegment(ctrl->segmentSize);
        ctrl->capacity = ctrl->segmentCount * ctrl->segmentSize; // 更新缓存容量 / update cached capacity
    }

public:
    static const size_t SEGMENT_SIZE;
    static const size_t SEGMENT_SHIFT;
    static const size_t SEGMENT_MASK;

    SegmentVector() {
        ctrl = new ControlBlock();
        ctrl->size = 0;
        ctrl->segmentCount = 0;
        ctrl->segmentCapacity = 4;
        ctrl->segmentSize = SEGMENT_SIZE;
        ctrl->capacity = 0;
        ctrl->refCount = 1;
        ctrl->segments = allocateSegmentArray(ctrl->segmentCapacity);
    }

    ~SegmentVector() {
        if (ctrl && --ctrl->refCount == 0) {
            freeControlBlock(ctrl);
        }
    }

    SegmentVector(const SegmentVector& other) {
        ctrl = other.ctrl;
        ++ctrl->refCount;
    }

    SegmentVector& operator=(const SegmentVector& other) {
        if (this != &other) {
            if (--ctrl->refCount == 0) {
                freeControlBlock(ctrl);
            }
            ctrl = other.ctrl;
            ++ctrl->refCount;
        }
        return *this;
    }

    SegmentVector(SegmentVector&& other) noexcept {
        ctrl = other.ctrl;
        other.ctrl = nullptr;
    }

    SegmentVector& operator=(SegmentVector&& other) noexcept {
        if (this != &other) {
            if (ctrl && --ctrl->refCount == 0) {
                freeControlBlock(ctrl);
            }
            ctrl = other.ctrl;
            other.ctrl = nullptr;
        }
        return *this;
    }

    size_t size() const { return ctrl->size; }

    void push_back(const T& val) {
        detach();
        size_t segSize = ctrl->size;
        if (segSize == ctrl->capacity) {
            addSegment();
        }
        size_t segIndex = segSize >> SEGMENT_SHIFT;
        size_t subIndex = segSize & SEGMENT_MASK;
        ctrl->segments[segIndex][subIndex] = val;
        ++ctrl->size;
    }

    T& operator[](size_t idx) {
        detach();
        assert(idx < ctrl->size);
        size_t segIndex = idx >> SEGMENT_SHIFT;
        size_t subIndex = idx & SEGMENT_MASK;
        return ctrl->segments[segIndex][subIndex];
    }

    const T& operator[](size_t idx) const {
        assert(idx < ctrl->size);
        size_t segIndex = idx >> SEGMENT_SHIFT;
        size_t subIndex = idx & SEGMENT_MASK;
        return ctrl->segments[segIndex][subIndex];
    }
};

template<typename T>
std::stack<T*> SegmentVector<T>::_freeSegments;

template<typename T>
const size_t SegmentVector<T>::SEGMENT_SIZE = getPageSize() / sizeof(T);

template<typename T>
const size_t SegmentVector<T>::SEGMENT_SHIFT = getSegmentShift(SegmentVector<T>::SEGMENT_SIZE);

template<typename T>
const size_t SegmentVector<T>::SEGMENT_MASK = getSegmentMask(SegmentVector<T>::SEGMENT_SIZE);
