// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * PooledVector.hpp
 *
 * 基于 BlockMemPool 的动态数组容器，接口风格参考 std::vector。
 */

#pragma once

#include "../Memory/BlockMemPool.hpp"
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <utility>

template<typename T>
class PooledVector {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    PooledVector() = default;

    explicit PooledVector(size_type count) {
        resize(count);
    }

    PooledVector(size_type count, const T& value) {
        reserve(count);
        for (size_type i = 0; i < count; ++i) {
            emplace_back(value);
        }
    }

    PooledVector(std::initializer_list<T> init) {
        reserve(init.size());
        for (const auto& value : init) {
            emplace_back(value);
        }
    }

    PooledVector(const PooledVector& other) {
        reserve(other._size);
        for (const auto& value : other) {
            emplace_back(value);
        }
    }

    PooledVector(PooledVector&& other) noexcept
        : _storage(std::move(other._storage)),
          _data(other._data),
          _size(other._size),
          _capacity(other._capacity) {
        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
    }

    ~PooledVector() {
        destroy_elements(0, _size);
    }

    PooledVector& operator=(const PooledVector& other) {
        if (this == &other) {
            return *this;
        }

        clear();
        reserve(other._size);
        for (const auto& value : other) {
            emplace_back(value);
        }
        return *this;
    }

    PooledVector& operator=(PooledVector&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy_elements(0, _size);
        _storage = std::move(other._storage);
        _data = other._data;
        _size = other._size;
        _capacity = other._capacity;

        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
        return *this;
    }

    reference at(size_type pos) {
        if (pos >= _size) {
            throw std::out_of_range("PooledVector::at out of range");
        }
        return _data[pos];
    }

    const_reference at(size_type pos) const {
        if (pos >= _size) {
            throw std::out_of_range("PooledVector::at out of range");
        }
        return _data[pos];
    }

    reference operator[](size_type pos) noexcept { return _data[pos]; }
    const_reference operator[](size_type pos) const noexcept { return _data[pos]; }
    reference front() noexcept { return _data[0]; }
    const_reference front() const noexcept { return _data[0]; }
    reference back() noexcept { return _data[_size - 1]; }
    const_reference back() const noexcept { return _data[_size - 1]; }
    pointer data() noexcept { return _data; }
    const_pointer data() const noexcept { return _data; }

    iterator begin() noexcept { return _data; }
    const_iterator begin() const noexcept { return _data; }
    const_iterator cbegin() const noexcept { return _data; }
    iterator end() noexcept { return _data + _size; }
    const_iterator end() const noexcept { return _data + _size; }
    const_iterator cend() const noexcept { return _data + _size; }

    bool empty() const noexcept { return _size == 0; }
    size_type size() const noexcept { return _size; }
    size_type capacity() const noexcept { return _capacity; }

    void reserve(size_type newCapacity) {
        if (newCapacity <= _capacity) {
            return;
        }

        auto newStorage = pool_instance().allocate(std::max<std::size_t>(sizeof(T) * newCapacity, sizeof(T)));
        T* newData = reinterpret_cast<T*>(newStorage.bytes());

        size_type i = 0;
        try {
            for (; i < _size; ++i) {
                new (newData + i) T(std::move_if_noexcept(_data[i]));
            }
        } catch (...) {
            for (size_type j = 0; j < i; ++j) {
                newData[j].~T();
            }
            throw;
        }

        destroy_elements(0, _size);
        _storage = std::move(newStorage);
        _data = newData;
        _capacity = _storage.byte_capacity() / sizeof(T);
    }

    void shrink_to_fit() {
        if (_size == _capacity) {
            return;
        }
        if (_size == 0) {
            clear();
            _storage = {};
            _data = nullptr;
            _capacity = 0;
            return;
        }

        auto newStorage = pool_instance().allocate(sizeof(T) * _size);
        T* newData = reinterpret_cast<T*>(newStorage.bytes());

        size_type i = 0;
        try {
            for (; i < _size; ++i) {
                new (newData + i) T(std::move_if_noexcept(_data[i]));
            }
        } catch (...) {
            for (size_type j = 0; j < i; ++j) {
                newData[j].~T();
            }
            throw;
        }

        destroy_elements(0, _size);
        _storage = std::move(newStorage);
        _data = newData;
        _capacity = _size;
    }

    void clear() noexcept {
        destroy_elements(0, _size);
        _size = 0;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        ensure_capacity_for_one_more();
        new (_data + _size) T(std::forward<Args>(args)...);
        ++_size;
        return back();
    }

    void push_back(const T& value) { emplace_back(value); }
    void push_back(T&& value) { emplace_back(std::move(value)); }

    void pop_back() {
        if (_size == 0) {
            return;
        }
        --_size;
        _data[_size].~T();
    }

    template<typename... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        const size_type index = static_cast<size_type>(pos - cbegin());
        ensure_capacity_for_one_more();

        if (index == _size) {
            emplace_back(std::forward<Args>(args)...);
            return begin() + index;
        }

        new (_data + _size) T(std::move_if_noexcept(_data[_size - 1]));
        for (size_type i = _size - 1; i > index; --i) {
            _data[i] = std::move_if_noexcept(_data[i - 1]);
        }
        _data[index].~T();
        new (_data + index) T(std::forward<Args>(args)...);
        ++_size;
        return begin() + index;
    }

    iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }
    iterator insert(const_iterator pos, T&& value) { return emplace(pos, std::move(value)); }

    iterator erase(const_iterator pos) {
        const size_type index = static_cast<size_type>(pos - cbegin());
        if (index >= _size) {
            return end();
        }

        _data[index].~T();
        for (size_type i = index; i + 1 < _size; ++i) {
            new (_data + i) T(std::move_if_noexcept(_data[i + 1]));
            _data[i + 1].~T();
        }
        --_size;
        return begin() + index;
    }

    void resize(size_type count) {
        if (count < _size) {
            destroy_elements(count, _size);
            _size = count;
            return;
        }

        reserve(count);
        while (_size < count) {
            emplace_back();
        }
    }

    void resize(size_type count, const value_type& value) {
        if (count < _size) {
            destroy_elements(count, _size);
            _size = count;
            return;
        }

        reserve(count);
        while (_size < count) {
            emplace_back(value);
        }
    }

private:
    static BlockMemPool& pool_instance() {
        static BlockMemPool pool;
        return pool;
    }

    void ensure_capacity_for_one_more() {
        if (_size < _capacity) {
            return;
        }
        reserve(_capacity == 0 ? 1 : (_capacity * 2));
    }

    void destroy_elements(size_type from, size_type to) noexcept {
        if (!std::is_trivially_destructible<T>::value) {
            for (size_type i = from; i < to; ++i) {
                _data[i].~T();
            }
        }
    }

private:
    BlockMemPool::ByteBlock _storage;
    T* _data = nullptr;
    size_type _size = 0;
    size_type _capacity = 0;
};
