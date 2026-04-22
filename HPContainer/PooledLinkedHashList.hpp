// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * PooledLinkedHashList.hpp
 *
 * 基于 PooledHashMap + PooledList 的有序哈希容器。
 * 支持通过 key 快速访问，同时通过链表维护插入/访问顺序，适用于 LRU 缓存。
 * 池化分配减少内存碎片和分配开销，特别适合频繁插入/删除且对性能敏感的场景。
 */

#pragma once

#include "PooledHashMap.hpp"
#include "PooledList.hpp"
#include <stdexcept>
#include <utility>

template<typename Key,
         typename T,
         typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>>
class PooledLinkedHashList {
public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using size_type = std::size_t;
    using list_type = PooledList<value_type>;
    using node_type = typename list_type::LinkedListNode;

    PooledLinkedHashList() = default;
    ~PooledLinkedHashList() = default;

    bool empty() const noexcept { return _list.empty(); }
    size_type size() const noexcept { return _list.size(); }

    bool contains(const key_type& key) const {
        return _index.find(key) != _index.cend();
    }

    mapped_type& at(const key_type& key) {
        auto it = _index.find(key);
        if (it == _index.end()) {
            throw std::out_of_range("PooledLinkedHashList::at key not found");
        }
        return it->second->value.second;
    }

    const mapped_type& at(const key_type& key) const {
        auto it = _index.find(key);
        if (it == _index.cend()) {
            throw std::out_of_range("PooledLinkedHashList::at key not found");
        }
        return it->second->value.second;
    }

    mapped_type* find(const key_type& key) noexcept {
        auto it = _index.find(key);
        return it == _index.end() ? nullptr : &(it->second->value.second);
    }

    const mapped_type* find(const key_type& key) const noexcept {
        auto it = _index.find(key);
        return it == _index.cend() ? nullptr : &(it->second->value.second);
    }

    value_type& front() {
        return _list.front();
    }

    const value_type& front() const {
        return _list.front();
    }

    value_type& back() {
        return _list.back();
    }

    const value_type& back() const {
        return _list.back();
    }

    std::pair<mapped_type*, bool> insert_or_assign_front(const key_type& key, const mapped_type& value) {
        return insert_or_assign_impl(key, value, true);
    }

    std::pair<mapped_type*, bool> insert_or_assign_front(key_type&& key, mapped_type&& value) {
        return insert_or_assign_impl(std::move(key), std::move(value), true);
    }

    std::pair<mapped_type*, bool> insert_or_assign_back(const key_type& key, const mapped_type& value) {
        return insert_or_assign_impl(key, value, false);
    }

    std::pair<mapped_type*, bool> insert_or_assign_back(key_type&& key, mapped_type&& value) {
        return insert_or_assign_impl(std::move(key), std::move(value), false);
    }

    template<typename... Args>
    std::pair<mapped_type*, bool> emplace_front(const key_type& key, Args&&... args) {
        return emplace_impl(key, true, std::forward<Args>(args)...);
    }

    template<typename... Args>
    std::pair<mapped_type*, bool> emplace_back(const key_type& key, Args&&... args) {
        return emplace_impl(key, false, std::forward<Args>(args)...);
    }

    bool erase(const key_type& key) {
        auto it = _index.find(key);
        if (it == _index.end()) {
            return false;
        }

        node_type* node = it->second;
        _index.erase(key);
        _list.erase(node);
        return true;
    }

    bool move_to_front(const key_type& key) {
        return relocate_node(key, true);
    }

    bool move_to_back(const key_type& key) {
        return relocate_node(key, false);
    }

    bool touch(const key_type& key) {
        return move_to_front(key);
    }

    bool pop_front() {
        if (_list.empty()) {
            return false;
        }
        const key_type key = _list.front().first;
        _index.erase(key);
        _list.pop_front();
        return true;
    }

    bool pop_back() {
        if (_list.empty()) {
            return false;
        }
        const key_type key = _list.back().first;
        _index.erase(key);
        _list.pop_back();
        return true;
    }

    bool pop_front(value_type& out) {
        if (_list.empty()) {
            return false;
        }
        out = _list.front();
        _index.erase(out.first);
        _list.pop_front();
        return true;
    }

    bool pop_back(value_type& out) {
        if (_list.empty()) {
            return false;
        }
        out = _list.back();
        _index.erase(out.first);
        _list.pop_back();
        return true;
    }

    void clear() {
        _index.clear();
        _list.clear();
    }

    template<typename Func>
    void for_each(Func&& func) {
        _list.for_each([&func](value_type& value) {
            func(value.first, value.second);
        });
    }

    template<typename Func>
    void for_each_const(Func&& func) const {
        _list.for_each_const([&func](const value_type& value) {
            func(value.first, value.second);
        });
    }

private:
    template<typename KeyArg, typename ValueArg>
    std::pair<mapped_type*, bool> insert_or_assign_impl(KeyArg&& key, ValueArg&& value, bool toFront) {
        auto it = _index.find(key);
        if (it != _index.end()) {
            node_type* node = it->second;
            node->value.second = std::forward<ValueArg>(value);
            if (toFront) {
                relocate_existing_node(it->first, node, true);
            } else {
                relocate_existing_node(it->first, node, false);
            }
            return {&(it->second->value.second), false};
        }

        node_type* node = append_new_node(std::forward<KeyArg>(key), std::forward<ValueArg>(value), toFront);
        return {&(node->value.second), true};
    }

    template<typename... Args>
    std::pair<mapped_type*, bool> emplace_impl(const key_type& key, bool toFront, Args&&... args) {
        auto it = _index.find(key);
        if (it != _index.end()) {
            return {&(it->second->value.second), false};
        }

        node_type* node = append_new_node(key, mapped_type(std::forward<Args>(args)...), toFront);
        return {&(node->value.second), true};
    }

    template<typename KeyArg, typename ValueArg>
    node_type* append_new_node(KeyArg&& key, ValueArg&& value, bool toFront) {
        if (toFront) {
            _list.push_front(std::forward<KeyArg>(key), std::forward<ValueArg>(value));
            node_type* node = _list.nodeAt(0);
            _index[node->value.first] = node;
            return node;
        }

        _list.push_back(std::forward<KeyArg>(key), std::forward<ValueArg>(value));
        node_type* node = _list.nodeAt(_list.size() - 1);
        _index[node->value.first] = node;
        return node;
    }

    bool relocate_node(const key_type& key, bool toFront) {
        auto it = _index.find(key);
        if (it == _index.end()) {
            return false;
        }
        relocate_existing_node(it->first, it->second, toFront);
        return true;
    }

    void relocate_existing_node(const key_type& key, node_type* node, bool toFront) {
        if (node == nullptr) {
            return;
        }

        if (toFront && node == _list.nodeAt(0)) {
            return;
        }
        if (!toFront && node == _list.nodeAt(_list.size() - 1)) {
            return;
        }

        value_type movedValue(std::move(node->value.first), std::move(node->value.second));
        _list.erase(node);

        node_type* newNode = nullptr;
        if (toFront) {
            _list.push_front(std::move(movedValue.first), std::move(movedValue.second));
            newNode = _list.nodeAt(0);
        } else {
            _list.push_back(std::move(movedValue.first), std::move(movedValue.second));
            newNode = _list.nodeAt(_list.size() - 1);
        }

        _index[key] = newNode;
    }

private:
    list_type _list;
    PooledHashMap<key_type, node_type*, Hash, KeyEqual> _index;
};
