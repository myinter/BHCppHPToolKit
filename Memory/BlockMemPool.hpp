// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * BlockMemPool.hpp
 *
 * 文件介绍 / Description:
 * 以页大小为最小扩容单位的块内存池，针对 64 / 128 / 256 / 512 / 1024 / 2048 / 4096
 * 等 2 的幂大小进行分桶预分配。对外返回带引用计数的 Block 句柄，可按指定类型访问。
 *
 * A page-granularity block memory pool that pre-allocates buckets for power-of-two
 * block sizes such as 64 / 128 / 256 / 512 / 1024 / 2048 / 4096. It returns a
 * reference-counted Block handle that can be viewed as a typed array.
 */

#pragma once

#include "../Foundations/MathBits.hpp"
#include "../MultiPlatforms/PlatformMemory.hpp"
#include "../MultiThreadAndMutex/BHSync.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class BlockMemPool {
private:
    struct Segment;
    struct Bucket;

public:
    template<typename T = uint8_t>
    class Block {
    public:
        static_assert(std::is_trivially_copyable<T>::value,
                      "Block<T> requires T to be trivially copyable");

        Block() = default;

        Block(const Block& other) noexcept
            : _segment(other._segment),
              _slotIndex(other._slotIndex),
              _data(other._data),
              _byteSize(other._byteSize),
              _byteCapacity(other._byteCapacity) {
            retain();
        }

        Block& operator=(const Block& other) noexcept {
            if (this == &other) {
                return *this;
            }

            release();

            _segment = other._segment;
            _slotIndex = other._slotIndex;
            _data = other._data;
            _byteSize = other._byteSize;
            _byteCapacity = other._byteCapacity;

            retain();
            return *this;
        }

        Block(Block&& other) noexcept
            : _segment(other._segment),
              _slotIndex(other._slotIndex),
              _data(other._data),
              _byteSize(other._byteSize),
              _byteCapacity(other._byteCapacity) {
            other.detach();
        }

        Block& operator=(Block&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            release();

            _segment = other._segment;
            _slotIndex = other._slotIndex;
            _data = other._data;
            _byteSize = other._byteSize;
            _byteCapacity = other._byteCapacity;

            other.detach();
            return *this;
        }

        ~Block() {
            release();
        }

        T* data() noexcept { return reinterpret_cast<T*>(_data); }
        const T* data() const noexcept { return reinterpret_cast<const T*>(_data); }

        uint8_t* bytes() noexcept { return _data; }
        const uint8_t* bytes() const noexcept { return _data; }

        std::size_t size() const noexcept { return _byteSize / sizeof(T); }
        std::size_t capacity() const noexcept { return _byteCapacity / sizeof(T); }
        std::size_t byte_size() const noexcept { return _byteSize; }
        std::size_t byte_capacity() const noexcept { return _byteCapacity; }

        bool empty() const noexcept { return _byteSize == 0; }
        explicit operator bool() const noexcept { return _data != nullptr; }

        std::size_t use_count() const noexcept;

        T& operator[](std::size_t index) noexcept { return data()[index]; }
        const T& operator[](std::size_t index) const noexcept { return data()[index]; }

        T* begin() noexcept { return data(); }
        T* end() noexcept { return data() + size(); }
        const T* begin() const noexcept { return data(); }
        const T* end() const noexcept { return data() + size(); }

        void fill(const T& value) noexcept {
            T* ptr = data();
            for (std::size_t i = 0; i < size(); ++i) {
                ptr[i] = value;
            }
        }

        template<typename U>
        Block<U> as() const noexcept {
            static_assert(std::is_trivially_copyable<U>::value,
                          "Block<U> requires U to be trivially copyable");
            if (_data == nullptr || (_byteSize % sizeof(U)) != 0 || (_byteCapacity % sizeof(U)) != 0) {
                return {};
            }
            return Block<U>(_segment, _slotIndex, _data, _byteSize, _byteCapacity, true);
        }

    private:
        friend class BlockMemPool;

        Block(Segment* segment,
              std::size_t slotIndex,
              uint8_t* data,
              std::size_t byteSize,
              std::size_t byteCapacity,
              bool addRef) noexcept
            : _segment(segment),
              _slotIndex(slotIndex),
              _data(data),
              _byteSize(byteSize),
              _byteCapacity(byteCapacity) {
            if (addRef) {
                retain();
            }
        }

        void retain() noexcept;
        void release() noexcept;

        void detach() noexcept {
            _segment = nullptr;
            _slotIndex = 0;
            _data = nullptr;
            _byteSize = 0;
            _byteCapacity = 0;
        }

        Segment* _segment = nullptr;
        std::size_t _slotIndex = 0;
        uint8_t* _data = nullptr;
        std::size_t _byteSize = 0;
        std::size_t _byteCapacity = 0;
    };

    using ByteBlock = Block<uint8_t>;

    explicit BlockMemPool(std::size_t minBlockSize = 64,
                          std::size_t maxPreallocatedBlockSize = 4096,
                          std::size_t pagesPerSegment = 1)
        : _pageSize(multi_platforms::os_page_size()),
          _minBlockSize(normalizeMinBlockSize(minBlockSize)),
          _maxPreallocatedBlockSize(normalizeMaxBlockSize(maxPreallocatedBlockSize, _minBlockSize)),
          _pagesPerSegment(pagesPerSegment == 0 ? 1 : pagesPerSegment) {}

    ~BlockMemPool() {
        clear();
    }

    BlockMemPool(const BlockMemPool&) = delete;
    BlockMemPool& operator=(const BlockMemPool&) = delete;
    BlockMemPool(BlockMemPool&&) = delete;
    BlockMemPool& operator=(BlockMemPool&&) = delete;

    ByteBlock allocate(std::size_t requestedBytes, bool zeroInitialize = false) {
        if (requestedBytes == 0) {
            return {};
        }

        Bucket& bucket = bucketForSize(requestedBytes);
        Segment* segment = nullptr;
        std::size_t slotIndex = 0;

        bucket.lock.lock();

        for (Segment* current = bucket.head; current != nullptr; current = current->next) {
            if (tryAcquireSlot(*current, slotIndex)) {
                segment = current;
                break;
            }
        }

        if (segment == nullptr) {
            segment = createSegment(bucket.blockSize);
            segment->next = bucket.head;
            bucket.head = segment;
            const bool acquired = tryAcquireSlot(*segment, slotIndex);
            bucket.segmentCount++;
            bucket.lock.unlock();

            if (acquired == false) {
                throw std::runtime_error("BlockMemPool failed to acquire slot from a new segment");
            }
        } else {
            bucket.lock.unlock();
        }

        uint8_t* data = segment->data + slotIndex * segment->blockSize;
        if (zeroInitialize) {
            std::memset(data, 0, requestedBytes);
        }

        return ByteBlock(segment, slotIndex, data, requestedBytes, segment->blockSize, true);
    }

    ByteBlock allocateZeroed(std::size_t requestedBytes) {
        return allocate(requestedBytes, true);
    }

    template<typename T>
    Block<T> allocateAs(std::size_t count = 1, bool zeroInitialize = false) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "allocateAs<T> requires T to be trivially copyable");
        if (count == 0) {
            return {};
        }
        const std::size_t requestedBytes = sizeof(T) * count;
        ByteBlock block = allocate(requestedBytes, zeroInitialize);
        Block<T> typedBlock(block._segment, block._slotIndex, block._data, requestedBytes, block._byteCapacity, false);
        block.detach();
        return typedBlock;
    }

    std::size_t pageSize() const noexcept { return _pageSize; }
    std::size_t minBlockSize() const noexcept { return _minBlockSize; }
    std::size_t maxPreallocatedBlockSize() const noexcept { return _maxPreallocatedBlockSize; }

    std::size_t bucketCount() const noexcept {
        _bucketMapLock.readLock();
        const std::size_t count = _bucketOrder.size();
        _bucketMapLock.readUnlock();
        return count;
    }

    std::vector<std::size_t> bucketSizes() const {
        _bucketMapLock.readLock();
        std::vector<std::size_t> sizes = _bucketOrder;
        _bucketMapLock.readUnlock();
        return sizes;
    }

    std::size_t totalSegmentCount() const noexcept {
        std::size_t count = 0;
        _bucketMapLock.readLock();
        typename std::unordered_map<std::size_t, std::unique_ptr<Bucket> >::const_iterator it = _buckets.begin();
        for (; it != _buckets.end(); ++it) {
            it->second->lock.lock();
            count += it->second->segmentCount;
            it->second->lock.unlock();
        }
        _bucketMapLock.readUnlock();
        return count;
    }

    void clear() noexcept {
        _bucketMapLock.writeLock();
        typename std::unordered_map<std::size_t, std::unique_ptr<Bucket> >::iterator it = _buckets.begin();
        for (; it != _buckets.end(); ++it) {
            destroyBucket(it->second.get());
        }
        _buckets.clear();
        _bucketOrder.clear();
        _bucketMapLock.writeUnlock();
    }

private:
    struct Segment {
        Segment(std::size_t slotSize,
                std::size_t slotCount,
                std::size_t storageBytes)
            : data(new uint8_t[storageBytes]),
              refCounts(new std::atomic<uint32_t>[slotCount]),
              bitmap(new uint64_t[(slotCount + 63) / 64]()) ,
              blockSize(slotSize),
              capacity(slotCount),
              bitmapWordCount((slotCount + 63) / 64),
              storageSize(storageBytes) {
            for (std::size_t i = 0; i < capacity; ++i) {
                refCounts[i].store(0, std::memory_order_relaxed);
            }
        }

        ~Segment() {
            delete[] data;
            delete[] refCounts;
            delete[] bitmap;
        }

        uint8_t* data = nullptr;
        std::atomic<uint32_t>* refCounts = nullptr;
        uint64_t* bitmap = nullptr;
        SmartLock lock;
        std::size_t blockSize = 0;
        std::size_t capacity = 0;
        std::size_t bitmapWordCount = 0;
        std::size_t storageSize = 0;
        std::size_t usedCount = 0;
        Segment* next = nullptr;
    };

    struct Bucket {
        explicit Bucket(std::size_t size) : blockSize(size) {}

        SmartLock lock;
        Segment* head = nullptr;
        std::size_t segmentCount = 0;
        std::size_t blockSize = 0;
    };

    static std::size_t normalizeMinBlockSize(std::size_t value) noexcept {
        const std::size_t candidate = std::max<std::size_t>(64, value);
        return foudations::is_power_of_two(candidate)
            ? candidate
            : foudations::round_up_power_of_two(candidate);
    }

    static std::size_t normalizeMaxBlockSize(std::size_t value, std::size_t minBlockSize) noexcept {
        const std::size_t candidate = std::max(value, minBlockSize);
        return foudations::is_power_of_two(candidate)
            ? candidate
            : foudations::round_up_power_of_two(candidate);
    }

    std::size_t chooseBucketSize(std::size_t requestedSize) const noexcept {
        const std::size_t normalized = std::max(requestedSize, _minBlockSize);
        const std::size_t bucketSize = foudations::round_up_power_of_two(normalized);
        return std::max(bucketSize, _minBlockSize);
    }

    Segment* createSegment(std::size_t blockSize) const {
        const std::size_t minimumBytes = blockSize > _maxPreallocatedBlockSize
            ? blockSize
            : blockSize * (_pageSize / std::min(blockSize, _pageSize));
        const std::size_t desiredBytes = std::max(minimumBytes, _pagesPerSegment * _pageSize);
        const std::size_t storageBytes = foudations::round_up_to_multiple(desiredBytes, _pageSize);
        const std::size_t slotCount = std::max<std::size_t>(1, storageBytes / blockSize);
        return new Segment(blockSize, slotCount, slotCount * blockSize);
    }

    Bucket& bucketForSize(std::size_t requestedSize) {
        const std::size_t bucketSize = chooseBucketSize(requestedSize);

        _bucketMapLock.readLock();
        typename std::unordered_map<std::size_t, std::unique_ptr<Bucket> >::iterator found = _buckets.find(bucketSize);
        if (found != _buckets.end()) {
            Bucket& bucket = *found->second;
            _bucketMapLock.readUnlock();
            return bucket;
        }
        _bucketMapLock.readUnlock();

        _bucketMapLock.writeLock();
        std::pair<typename std::unordered_map<std::size_t, std::unique_ptr<Bucket> >::iterator, bool> insertResult =
            _buckets.emplace(bucketSize, std::unique_ptr<Bucket>(new Bucket(bucketSize)));
        typename std::unordered_map<std::size_t, std::unique_ptr<Bucket> >::iterator it = insertResult.first;
        bool inserted = insertResult.second;
        if (inserted) {
            _bucketOrder.push_back(bucketSize);
        }
        Bucket& bucket = *it->second;
        _bucketMapLock.writeUnlock();
        return bucket;
    }

    static bool tryAcquireSlot(Segment& segment, std::size_t& slotIndex) noexcept {
        segment.lock.lock();

        if (segment.usedCount >= segment.capacity) {
            segment.lock.unlock();
            return false;
        }

        for (std::size_t wordIndex = 0; wordIndex < segment.bitmapWordCount; ++wordIndex) {
            const uint64_t word = segment.bitmap[wordIndex];
            if (word == UINT64_MAX) {
                continue;
            }

            const std::size_t bitIndex = foudations::find_first_zero_bit(word);
            const std::size_t candidateIndex = wordIndex * 64 + bitIndex;
            if (candidateIndex >= segment.capacity) {
                continue;
            }

            segment.bitmap[wordIndex] |= (uint64_t(1) << bitIndex);
            segment.refCounts[candidateIndex].store(0, std::memory_order_release);
            ++segment.usedCount;
            slotIndex = candidateIndex;
            segment.lock.unlock();
            return true;
        }

        segment.lock.unlock();
        return false;
    }

    static void releaseSlot(Segment& segment, std::size_t slotIndex) noexcept {
        segment.lock.lock();

        const std::size_t wordIndex = slotIndex / 64;
        const std::size_t bitIndex = slotIndex % 64;
        const uint64_t mask = uint64_t(1) << bitIndex;

        segment.refCounts[slotIndex].store(0, std::memory_order_release);
        segment.bitmap[wordIndex] &= ~mask;

        if (segment.usedCount > 0) {
            --segment.usedCount;
        }

        segment.lock.unlock();
    }

    static void destroyBucket(Bucket* bucket) noexcept {
        if (bucket == nullptr) {
            return;
        }

        bucket->lock.lock();
        Segment* current = bucket->head;
        bucket->head = nullptr;
        bucket->segmentCount = 0;
        bucket->lock.unlock();

        while (current != nullptr) {
            Segment* next = current->next;
            delete current;
            current = next;
        }
    }

private:
    const std::size_t _pageSize;
    const std::size_t _minBlockSize;
    const std::size_t _maxPreallocatedBlockSize;
    const std::size_t _pagesPerSegment;

    mutable RWSmartLock _bucketMapLock;
    std::unordered_map<std::size_t, std::unique_ptr<Bucket>> _buckets;
    std::vector<std::size_t> _bucketOrder;
};

template<typename T>
inline std::size_t BlockMemPool::Block<T>::use_count() const noexcept {
    if (_segment == nullptr) {
        return 0;
    }
    return _segment->refCounts[_slotIndex].load(std::memory_order_acquire);
}

template<typename T>
inline void BlockMemPool::Block<T>::retain() noexcept {
    if (_segment == nullptr) {
        return;
    }
    _segment->refCounts[_slotIndex].fetch_add(1, std::memory_order_acq_rel);
}

template<typename T>
inline void BlockMemPool::Block<T>::release() noexcept {
    if (_segment == nullptr) {
        return;
    }

    const uint32_t previous = _segment->refCounts[_slotIndex].fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 1) {
        BlockMemPool::releaseSlot(*_segment, _slotIndex);
    }

    detach();
}
