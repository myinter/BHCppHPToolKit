// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * PooledHashMap.hpp
 *
 * 基于 BlockMemPool分配内存空间 的哈希映射容器，接口风格参考 std::unordered_map。
 * 相当于池化分配版本的 std::unordered_map，适用于频繁插入/删除且对性能敏感的场景。
 * 特别是局部临时对象，池化分配内部空间可以显著减少内存碎片和分配开销。
 */

#pragma once

#include "../Foundations/MathBits.hpp"
#include "../Memory/BlockMemPool.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

template<typename Key,
         typename T,
         typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>>
class PooledHashMap {
public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;

private:
    struct Entry {
        enum State : uint8_t {
            Empty = 0,
            Occupied = 1,
            Erased = 2
        };

        size_type hash = 0;
        uint8_t state = Empty;
        alignas(value_type) unsigned char storage[sizeof(value_type)];

        value_type* value_ptr() noexcept {
            return reinterpret_cast<value_type*>(storage);
        }

        const value_type* value_ptr() const noexcept {
            return reinterpret_cast<const value_type*>(storage);
        }
    };

public:
    class iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = typename PooledHashMap::value_type;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;

        reference operator*() const noexcept { return *_map->_entries[_index].value_ptr(); }
        pointer operator->() const noexcept { return _map->_entries[_index].value_ptr(); }

        iterator& operator++() {
            advance();
            return *this;
        }

        iterator operator++(int) {
            iterator temp(*this);
            advance();
            return temp;
        }

        friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
            return lhs._map == rhs._map && lhs._index == rhs._index;
        }

        friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept {
            return !(lhs == rhs);
        }

    private:
        friend class PooledHashMap;

        iterator(PooledHashMap* map, size_type index) noexcept
            : _map(map), _index(index) {
            skip_to_occupied();
        }

        void advance() noexcept {
            ++_index;
            skip_to_occupied();
        }

        void skip_to_occupied() noexcept {
            while (_map && _index < _map->_bucketCount && _map->_entries[_index].state != Entry::Occupied) {
                ++_index;
            }
        }

        PooledHashMap* _map = nullptr;
        size_type _index = 0;
    };

    class const_iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = const typename PooledHashMap::value_type;
        using pointer = const typename PooledHashMap::value_type*;
        using reference = const typename PooledHashMap::value_type&;
        using iterator_category = std::forward_iterator_tag;

        const_iterator() = default;
        const_iterator(const iterator& other) noexcept : _map(other._map), _index(other._index) {}

        reference operator*() const noexcept { return *_map->_entries[_index].value_ptr(); }
        pointer operator->() const noexcept { return _map->_entries[_index].value_ptr(); }

        const_iterator& operator++() {
            advance();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator temp(*this);
            advance();
            return temp;
        }

        friend bool operator==(const const_iterator& lhs, const const_iterator& rhs) noexcept {
            return lhs._map == rhs._map && lhs._index == rhs._index;
        }

        friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs) noexcept {
            return !(lhs == rhs);
        }

    private:
        friend class PooledHashMap;

        const_iterator(const PooledHashMap* map, size_type index) noexcept
            : _map(map), _index(index) {
            skip_to_occupied();
        }

        void advance() noexcept {
            ++_index;
            skip_to_occupied();
        }

        void skip_to_occupied() noexcept {
            while (_map && _index < _map->_bucketCount && _map->_entries[_index].state != Entry::Occupied) {
                ++_index;
            }
        }

        const PooledHashMap* _map = nullptr;
        size_type _index = 0;
    };

    PooledHashMap() {
        rehash(8);
    }

    explicit PooledHashMap(size_type bucketCount) {
        rehash(bucketCount);
    }

    PooledHashMap(const PooledHashMap& other)
        : _hasher(other._hasher),
          _equal(other._equal),
          _maxLoadFactor(other._maxLoadFactor) {
        rehash(other._bucketCount == 0 ? 8 : other._bucketCount);
        for (const_iterator item = other.begin(); item != other.end(); ++item) {
            insert(*item);
        }
    }

    PooledHashMap(PooledHashMap&& other) noexcept
        : _storage(std::move(other._storage)),
          _entries(other._entries),
          _bucketCount(other._bucketCount),
          _size(other._size),
          _erasedCount(other._erasedCount),
          _hasher(std::move(other._hasher)),
          _equal(std::move(other._equal)),
          _maxLoadFactor(other._maxLoadFactor) {
        other._entries = nullptr;
        other._bucketCount = 0;
        other._size = 0;
        other._erasedCount = 0;
    }

    ~PooledHashMap() {
        destroy_all_entries();
    }

    PooledHashMap& operator=(const PooledHashMap& other) {
        if (this == &other) {
            return *this;
        }

        clear();
        _hasher = other._hasher;
        _equal = other._equal;
        _maxLoadFactor = other._maxLoadFactor;
        rehash(other._bucketCount == 0 ? 8 : other._bucketCount);
        for (const_iterator item = other.begin(); item != other.end(); ++item) {
            insert(*item);
        }
        return *this;
    }

    PooledHashMap& operator=(PooledHashMap&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy_all_entries();
        _storage = std::move(other._storage);
        _entries = other._entries;
        _bucketCount = other._bucketCount;
        _size = other._size;
        _erasedCount = other._erasedCount;
        _hasher = std::move(other._hasher);
        _equal = std::move(other._equal);
        _maxLoadFactor = other._maxLoadFactor;

        other._entries = nullptr;
        other._bucketCount = 0;
        other._size = 0;
        other._erasedCount = 0;
        return *this;
    }

    bool empty() const noexcept { return _size == 0; }
    size_type size() const noexcept { return _size; }
    size_type bucket_count() const noexcept { return _bucketCount; }

    float load_factor() const noexcept {
        return _bucketCount == 0 ? 0.0f : static_cast<float>(_size) / static_cast<float>(_bucketCount);
    }

    float max_load_factor() const noexcept {
        return _maxLoadFactor;
    }

    void max_load_factor(float value) noexcept {
        _maxLoadFactor = value > 0.1f ? value : 0.1f;
    }

    iterator begin() noexcept { return iterator(this, 0); }
    iterator end() noexcept { return iterator(this, _bucketCount); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    const_iterator end() const noexcept { return const_iterator(this, _bucketCount); }
    const_iterator cbegin() const noexcept { return const_iterator(this, 0); }
    const_iterator cend() const noexcept { return const_iterator(this, _bucketCount); }

    mapped_type& operator[](const key_type& key) {
        std::pair<iterator, bool> result = try_emplace(key);
        iterator it = result.first;
        bool inserted = result.second;
        (void)inserted;
        return it->second;
    }

    mapped_type& operator[](key_type&& key) {
        std::pair<iterator, bool> result = try_emplace(std::move(key));
        iterator it = result.first;
        bool inserted = result.second;
        (void)inserted;
        return it->second;
    }

    mapped_type& at(const key_type& key) {
        auto it = find(key);
        if (it == end()) {
            throw std::out_of_range("PooledHashMap::at key not found");
        }
        return it->second;
    }

    const mapped_type& at(const key_type& key) const {
        auto it = find(key);
        if (it == cend()) {
            throw std::out_of_range("PooledHashMap::at key not found");
        }
        return it->second;
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return emplace(value.first, value.second);
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        return emplace(value.first, std::move(value.second));
    }

    template<typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        value_type value(std::forward<Args>(args)...);
        return insert_value(std::move(value));
    }

    template<typename... Args>
    std::pair<iterator, bool> try_emplace(const key_type& key, Args&&... args) {
        ensure_capacity_for_insert();
        const size_type hashValue = _hasher(key);
        size_type index = 0;
        Entry* slot = find_slot_for_insert(key, hashValue, index);
        if (slot->state == Entry::Occupied) {
            return {iterator(this, index), false};
        }

        const bool reusingErased = (slot->state == Entry::Erased);
        construct_entry(slot, hashValue, value_type(key, mapped_type(std::forward<Args>(args)...)));
        ++_size;
        if (reusingErased && _erasedCount > 0) {
            --_erasedCount;
        }
        return {iterator(this, index), true};
    }

    template<typename... Args>
    std::pair<iterator, bool> try_emplace(key_type&& key, Args&&... args) {
        ensure_capacity_for_insert();
        const size_type hashValue = _hasher(key);
        size_type index = 0;
        Entry* slot = find_slot_for_insert(key, hashValue, index);
        if (slot->state == Entry::Occupied) {
            return {iterator(this, index), false};
        }

        const bool reusingErased = (slot->state == Entry::Erased);
        construct_entry(slot, hashValue, value_type(std::move(key), mapped_type(std::forward<Args>(args)...)));
        ++_size;
        if (reusingErased && _erasedCount > 0) {
            --_erasedCount;
        }
        return {iterator(this, index), true};
    }

    iterator find(const key_type& key) noexcept {
        if (_bucketCount == 0) {
            return end();
        }

        size_type index = find_existing_index(key);
        return index == _bucketCount ? end() : iterator(this, index);
    }

    const_iterator find(const key_type& key) const noexcept {
        if (_bucketCount == 0) {
            return cend();
        }

        size_type index = find_existing_index(key);
        return index == _bucketCount ? cend() : const_iterator(this, index);
    }

    bool contains(const key_type& key) const noexcept {
        return find_existing_index(key) != _bucketCount;
    }

    size_type erase(const key_type& key) {
        const size_type index = find_existing_index(key);
        if (index == _bucketCount) {
            return 0;
        }

        destroy_entry(_entries + index);
        _entries[index].state = Entry::Erased;
        _entries[index].hash = 0;
        --_size;
        ++_erasedCount;
        return 1;
    }

    iterator erase(iterator pos) {
        if (pos == end()) {
            return end();
        }
        const size_type index = pos._index;
        destroy_entry(_entries + index);
        _entries[index].state = Entry::Erased;
        _entries[index].hash = 0;
        --_size;
        ++_erasedCount;
        return iterator(this, index + 1);
    }

    void clear() noexcept {
        destroy_all_entries();
        if (_entries != nullptr && _bucketCount > 0) {
            std::memset(_entries, 0, sizeof(Entry) * _bucketCount);
        }
        _size = 0;
        _erasedCount = 0;
    }

    void reserve(size_type count) {
        const size_type required = static_cast<size_type>(static_cast<float>(count) / _maxLoadFactor) + 1;
        if (required > _bucketCount) {
            rehash(required);
        }
    }

    void rehash(size_type count) {
        const size_type newBucketCount = normalize_bucket_count(std::max<size_type>(count, 8));
        BlockMemPool::ByteBlock newStorage = pool_instance().allocate(sizeof(Entry) * newBucketCount, true);
        Entry* newEntries = reinterpret_cast<Entry*>(newStorage.bytes());
        std::memset(newEntries, 0, sizeof(Entry) * newBucketCount);

        Entry* oldEntries = _entries;
        const size_type oldBucketCount = _bucketCount;

        _storage = std::move(newStorage);
        _entries = newEntries;
        _bucketCount = newBucketCount;
        _size = 0;
        _erasedCount = 0;

        if (oldEntries == nullptr) {
            return;
        }

        for (size_type i = 0; i < oldBucketCount; ++i) {
            if (oldEntries[i].state != Entry::Occupied) {
                continue;
            }

            value_type* value = oldEntries[i].value_ptr();
            insert_rehashed_value(std::move(*value), oldEntries[i].hash);
            value->~value_type();
        }
    }

private:
    static BlockMemPool& pool_instance() {
        static BlockMemPool pool;
        return pool;
    }

    static size_type normalize_bucket_count(size_type value) noexcept {
        return foudations::round_up_power_of_two(value);
    }

    void ensure_capacity_for_insert() {
        if (_bucketCount == 0) {
            rehash(8);
            return;
        }

        if (static_cast<float>(_size + _erasedCount + 1) / static_cast<float>(_bucketCount) > _maxLoadFactor) {
            rehash(_bucketCount * 2);
        }
    }

    std::pair<iterator, bool> insert_value(value_type&& value) {
        ensure_capacity_for_insert();
        const size_type hashValue = _hasher(value.first);
        size_type index = 0;
        Entry* slot = find_slot_for_insert(value.first, hashValue, index);
        if (slot->state == Entry::Occupied) {
            return {iterator(this, index), false};
        }

        const bool reusingErased = (slot->state == Entry::Erased);
        construct_entry(slot, hashValue, std::move(value));
        ++_size;
        if (reusingErased && _erasedCount > 0) {
            --_erasedCount;
        }
        return {iterator(this, index), true};
    }

    void insert_rehashed_value(value_type&& value, size_type hashValue) {
        size_type index = 0;
        Entry* slot = find_slot_for_insert(value.first, hashValue, index);
        construct_entry(slot, hashValue, std::move(value));
        ++_size;
    }

    void construct_entry(Entry* entry, size_type hashValue, value_type&& value) {
        new (entry->value_ptr()) value_type(std::move(value));
        entry->hash = hashValue;
        entry->state = Entry::Occupied;
    }

    void destroy_entry(Entry* entry) noexcept {
        if (entry->state == Entry::Occupied) {
            entry->value_ptr()->~value_type();
        }
    }

    void destroy_all_entries() noexcept {
        if (_entries == nullptr) {
            return;
        }
        for (size_type i = 0; i < _bucketCount; ++i) {
            destroy_entry(_entries + i);
        }
    }

    size_type find_existing_index(const key_type& key) const noexcept {
        if (_bucketCount == 0) {
            return 0;
        }

        const size_type hashValue = _hasher(key);
        size_type index = hashValue & (_bucketCount - 1);

        for (size_type probe = 0; probe < _bucketCount; ++probe) {
            const Entry& entry = _entries[index];
            if (entry.state == Entry::Empty) {
                return _bucketCount;
            }
            if (entry.state == Entry::Occupied && entry.hash == hashValue && _equal(entry.value_ptr()->first, key)) {
                return index;
            }
            index = (index + 1) & (_bucketCount - 1);
        }

        return _bucketCount;
    }

    Entry* find_slot_for_insert(const key_type& key, size_type hashValue, size_type& foundIndex) noexcept {
        size_type index = hashValue & (_bucketCount - 1);
        Entry* firstErased = nullptr;
        size_type firstErasedIndex = _bucketCount;

        for (size_type probe = 0; probe < _bucketCount; ++probe) {
            Entry* entry = _entries + index;
            if (entry->state == Entry::Empty) {
                if (firstErased != nullptr) {
                    foundIndex = firstErasedIndex;
                    return firstErased;
                }
                foundIndex = index;
                return entry;
            }
            if (entry->state == Entry::Erased) {
                if (firstErased == nullptr) {
                    firstErased = entry;
                    firstErasedIndex = index;
                }
            } else if (entry->hash == hashValue && _equal(entry->value_ptr()->first, key)) {
                foundIndex = index;
                return entry;
            }
            index = (index + 1) & (_bucketCount - 1);
        }

        foundIndex = firstErasedIndex;
        return firstErased;
    }

private:
    BlockMemPool::ByteBlock _storage;
    Entry* _entries = nullptr;
    size_type _bucketCount = 0;
    size_type _size = 0;
    size_type _erasedCount = 0;
    Hash _hasher{};
    KeyEqual _equal{};
    float _maxLoadFactor = 0.75f;
};
