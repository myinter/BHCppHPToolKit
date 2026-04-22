// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * RWRingQueue.hpp
 *
 * 文件介绍 / Description:
 * 基于 RWSmartLock 实现的固定容量环形队列，适用于缓冲消息、日志事件、
 * 网络包等高频入队/出队场景。
 *
 * A fixed-capacity ring queue based on RWSmartLock. It is suitable for
 * buffering messages, log events, network packets, and other high-frequency
 * producer/consumer workloads.
 */

#pragma once

#include "../Memory/BlockMemPool.hpp"
#include "../MultiThreadAndMutex/BHSync.hpp"
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

template<typename T>
class RWRingQueue {
private:
    struct Slot {
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
        bool occupied;

        Slot() : occupied(false) {}

        T* value_ptr() {
            return reinterpret_cast<T*>(&storage);
        }

        const T* value_ptr() const {
            return reinterpret_cast<const T*>(&storage);
        }
    };

    mutable RWSmartLock _lock;
    BlockMemPool::ByteBlock _storage;
    Slot* _buffer;
    std::size_t _head = 0;
    std::size_t _tail = 0;
    std::size_t _size = 0;
    bool _overwriteWhenFull = false;
    bool _autoExpand = true;

    static BlockMemPool& pool_instance() {
        static BlockMemPool pool;
        return pool;
    }

    std::size_t nextIndex(std::size_t index) const noexcept {
        return (index + 1) % capacity();
    }

    void destroySlot(std::size_t index) {
        if (_buffer != nullptr && _buffer[index].occupied) {
            _buffer[index].value_ptr()->~T();
            _buffer[index].occupied = false;
        }
    }

    void initializeSlots(Slot* slots, std::size_t slotCount) {
        for (std::size_t i = 0; i < slotCount; ++i) {
            new (slots + i) Slot();
        }
    }

    void ensureCapacity(std::size_t minCapacity) {
        if (capacity() >= minCapacity) {
            return;
        }

        std::size_t newCapacity = capacity() == 0 ? 1 : capacity();
        while (newCapacity < minCapacity) {
            newCapacity *= 2;
        }

        BlockMemPool::ByteBlock newStorage = pool_instance().allocate(sizeof(Slot) * newCapacity, true);
        Slot* newBuffer = reinterpret_cast<Slot*>(newStorage.bytes());
        initializeSlots(newBuffer, newCapacity);

        for (std::size_t i = 0; i != _size; ++i) {
            const std::size_t oldIndex = (_head + i) % capacity();
            new (newBuffer[i].value_ptr()) T(std::move(*_buffer[oldIndex].value_ptr()));
            newBuffer[i].occupied = true;
            destroySlot(oldIndex);
        }

        _storage = std::move(newStorage);
        _buffer = newBuffer;
        _head = 0;
        _tail = _size;
    }

    template<typename ValueType>
    bool pushValue(ValueType&& value) {
        _lock.writeLock();

        if (_size == capacity()) {
            if (_autoExpand) {
                ensureCapacity(capacity() + 1);
            } else if (_overwriteWhenFull == false) {
                _lock.writeUnlock();
                return false;
            } else {
                destroySlot(_tail);
                new (_buffer[_tail].value_ptr()) T(std::forward<ValueType>(value));
                _buffer[_tail].occupied = true;
                _tail = nextIndex(_tail);
                _head = _tail;

                _lock.writeUnlock();
                return true;
            }
        }

        new (_buffer[_tail].value_ptr()) T(std::forward<ValueType>(value));
        _buffer[_tail].occupied = true;
        _tail = nextIndex(_tail);
        ++_size;

        _lock.writeUnlock();
        return true;
    }

public:
    explicit RWRingQueue(std::size_t capacity,
                         bool overwriteWhenFull = false,
                         bool autoExpand = true,
                         bool needSleep = true,
                         int spin = 5,
                         int yield = 8,
                         int sleep_us = 3)
        : _lock(needSleep, spin, yield, sleep_us),
          _storage(pool_instance().allocate(sizeof(Slot) * capacity, true)),
          _buffer(reinterpret_cast<Slot*>(_storage.bytes())),
          _overwriteWhenFull(overwriteWhenFull),
          _autoExpand(autoExpand) {
        if (capacity == 0) {
            throw std::invalid_argument("RWRingQueue capacity must be greater than 0");
        }
        initializeSlots(_buffer, capacity);
    }

    ~RWRingQueue() {
        clear();
    }

    RWRingQueue(const RWRingQueue&) = delete;
    RWRingQueue& operator=(const RWRingQueue&) = delete;
    RWRingQueue(RWRingQueue&&) = delete;
    RWRingQueue& operator=(RWRingQueue&&) = delete;

    std::size_t capacity() const noexcept {
        return _storage.byte_capacity() / sizeof(Slot);
    }

    bool overwriteWhenFull() const noexcept {
        return _overwriteWhenFull;
    }

    bool autoExpand() const noexcept {
        return _autoExpand;
    }

    void setOverwriteWhenFull(bool overwrite) {
        _lock.writeLock();
        _overwriteWhenFull = overwrite;
        _lock.writeUnlock();
    }

    void setAutoExpand(bool autoExpand) {
        _lock.writeLock();
        _autoExpand = autoExpand;
        _lock.writeUnlock();
    }

    void reserve(std::size_t requestedCapacity) {
        _lock.writeLock();
        ensureCapacity(requestedCapacity);
        _lock.writeUnlock();
    }

    std::size_t size() const noexcept {
        _lock.readLock();
        std::size_t currentSize = _size;
        _lock.readUnlock();
        return currentSize;
    }

    bool empty() const noexcept {
        return size() == 0;
    }

    bool full() const noexcept {
        _lock.readLock();
        bool isFull = (_size == capacity());
        _lock.readUnlock();
        return isFull;
    }

    template<typename ValueType>
    bool try_push(ValueType&& value) {
        return pushValue(std::forward<ValueType>(value));
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        _lock.writeLock();

        if (_size == capacity()) {
            if (_autoExpand) {
                ensureCapacity(capacity() + 1);
            } else if (_overwriteWhenFull == false) {
                _lock.writeUnlock();
                return false;
            } else {
                destroySlot(_tail);
                new (_buffer[_tail].value_ptr()) T(std::forward<Args>(args)...);
                _buffer[_tail].occupied = true;
                _tail = nextIndex(_tail);
                _head = _tail;

                _lock.writeUnlock();
                return true;
            }
        }

        new (_buffer[_tail].value_ptr()) T(std::forward<Args>(args)...);
        _buffer[_tail].occupied = true;
        _tail = nextIndex(_tail);
        ++_size;

        _lock.writeUnlock();
        return true;
    }

    bool try_pop(T& out) {
        _lock.writeLock();

        if (_size == 0) {
            _lock.writeUnlock();
            return false;
        }

        out = std::move(*_buffer[_head].value_ptr());
        destroySlot(_head);
        _head = nextIndex(_head);
        --_size;

        _lock.writeUnlock();
        return true;
    }

    bool front_copy(T& out) const {
        _lock.readLock();

        if (_size == 0) {
            _lock.readUnlock();
            return false;
        }

        out = *_buffer[_head].value_ptr();
        _lock.readUnlock();
        return true;
    }

    std::size_t pop_batch(std::vector<T>& out, std::size_t maxCount) {
        _lock.writeLock();

        const std::size_t actualCount = (_size < maxCount) ? _size : maxCount;
        out.reserve(out.size() + actualCount);

        for (std::size_t i = 0; i < actualCount; ++i) {
            out.push_back(std::move(*_buffer[_head].value_ptr()));
            destroySlot(_head);
            _head = nextIndex(_head);
        }

        _size -= actualCount;

        _lock.writeUnlock();
        return actualCount;
    }

    std::vector<T> snapshot() const {
        _lock.readLock();

        std::vector<T> result;
        result.reserve(_size);

        std::size_t index = _head;
        for (std::size_t i = 0; i < _size; ++i) {
            result.push_back(*_buffer[index].value_ptr());
            index = nextIndex(index);
        }

        _lock.readUnlock();
        return result;
    }

    void clear() {
        _lock.writeLock();

        for (std::size_t i = 0; i < capacity(); ++i) {
            destroySlot(i);
        }

        _head = 0;
        _tail = 0;
        _size = 0;

        _lock.writeUnlock();
    }
};
