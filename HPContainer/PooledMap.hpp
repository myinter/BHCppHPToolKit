// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * PooledMap.hpp
 *
 * PooledMap 功能介绍 / Features
 *
 * 功能介绍：
 * 1. 基于红黑树 (Red-Black Tree) 实现的有序映射容器。
 * 2. 使用分段式对象池 (Segmented Object Pool) 管理节点内存，避免频繁的内存申请与释放。
 * 3. 节点在内存中连续分配，提升 CPU Cache 命中率和整体插入性能。
 * 4. 接口设计参考 std::map，可作为高性能替代方案，且兼容常用用法。
 * 5. 特别适用于需要高频插入、删除和查找操作的场景，如实时计算、游戏引擎和交易系统。
 *
 * Features:
 * 1. An ordered map container implemented with a Red-Black Tree.
 * 2. Employs a Segmented Object Pool for node memory management,
 *    eliminating frequent allocations and deallocations.
 * 3. Nodes are allocated in contiguous memory blocks, improving CPU cache
 *    locality and insertion performance.
 * 4. Provides a std::map-like interface, making it a drop-in high-performance
 *    alternative in many use cases.
 * 5. Well-suited for scenarios requiring frequent insertions, deletions,
 *    and lookups, such as real-time computing, game engines, and trading systems.
 */

#pragma once

#include <iostream>
#include <vector>
#include <type_traits>
#include <string>
#include "../Memory/SegmentedObjectPool.hpp"

template <typename Key, typename Value>
class PooledMap {
public:
    PooledMap() = default;
    ~PooledMap() { clear(root); }

    // ---------- 公共接口 / Public interface ----------

    /**
     * @brief 插入或访问键值对 / Insert or access key-value pair
     *
     * 行为与 std::map::operator[] 一致：
     * - 如果 key 已存在，返回对应 value 的引用；
     * - 如果 key 不存在，则插入默认构造的 value，并返回其引用。
     *
     * Behavior same as std::map::operator[]:
     * - If key exists, return reference to its value;
     * - If key does not exist, insert default-constructed value and return reference.
     */
    Value& operator[](const Key& key) {
        NodeType* cur = root;
        NodeType* parent = nullptr;
        while (cur) {
            Key& k = cur->key;
            if (key == k) return cur->value;
            parent = cur;
            cur = (key < k) ? cur->left : cur->right;
        }

        // 创建新节点 / Create a new node
        NodeType* node = NodeType::create(key, Value());;

        node->left = node->right = node->parent = nullptr;
        node->color = RED;
        node->parent = parent;

        if (!root) root = node;
        else if (key < parent->key) parent->left = node;
        else parent->right = node;

        // 红黑树插入修复 / Fix RB-tree property after insert
        fix_insert(node);
        ++size_;
        return node->value;
    }

    /**
     * @brief 查找键对应的值 / Find value by key
     *
     * 如果找到返回对应值的拷贝，否则返回 Value() 默认值。
     * If found, returns a copy of the value; otherwise returns default Value().
     */
    Value find(const Key& key) {
        NodeType* cur = root;
        while (cur) {
            Key& k = cur->key;
            if (key == k) return cur->value;
            cur = (key < k) ? cur->left : cur->right;
        }
        return Value();
    }

    /**
     * @brief 删除指定 key 的节点 / Erase node by key
     *
     * - 如果删除成功，返回 1；
     * - 如果未找到，返回 0。
     *
     * - Returns 1 if erase success;
     * - Returns 0 if key not found.
     */
    std::size_t erase(const Key& key) {
        NodeType* z = root;
        while (z) {
            Key& k = z->key;
            if (key == k) break;
            z = (key < k) ? z->left : z->right;
        }
        if (!z) return 0;

        NodeType* y = z;
        Color y_original_color = y->color;
        NodeType* x = nullptr;
        NodeType* x_parent = nullptr;

        // 删除分支逻辑 / Different cases of deletion
        if (!z->left) {
            x = z->right;
            x_parent = z->parent;
            transplant(z, z->right);
        } else if (!z->right) {
            x = z->left;
            x_parent = z->parent;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_original_color = y->color;
            x = y->right;
            if (y->parent == z) {
                if (x) x->parent = y;
                x_parent = y;
            } else {
                transplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
                x_parent = y->parent;
            }
            transplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }

        z->recycle();  // 回收节点到对象池 / Recycle node to object pool
        --size_;

        if (y_original_color == BLACK)
            fix_erase(x, x_parent);

        return 1;
    }

    /// 返回当前大小 / Return current size
    std::size_t size() const noexcept { return size_; }

    /// 判断是否为空 / Check if empty
    bool empty() const noexcept { return size_ == 0; }

    /**
     * @brief 判断 key 是否存在 / Check if key exists
     */
    bool contains(const Key& key) const {
        NodeType* cur = root;
        while (cur) {
            Key& k = cur->key;
            if (key == k) return true;
            cur = (key < k) ? cur->left : cur->right;
        }
        return false;
    }

    // ---------- 对外唯一遍历方法 / Single public traversal method ----------
    /**
     * @brief 中序遍历所有 key-value / Traverse all key-value in-order
     *
     * 接收一个 lambda，参数为 `(const Key&, Value&)`。
     * Accepts a lambda with parameters `(const Key&, Value&)`.
     */
    template <typename Func>
    void for_each(Func&& func) {
        inorder_traverse(root, std::forward<Func>(func));
    }

private:
    // ---------- 内部定义 / Internal definitions ----------
    enum Color { RED, BLACK };

    /**
     * @brief 红黑树节点 / Red-black tree node
     *
     * 使用 PooledObject 基类进行对象池化分配。
     * Uses PooledObject base class for pooled allocation.
     */
    struct Node : public PooledObject<Node> {
        Key key;
        Value value;
        Node* left = nullptr;
        Node* right = nullptr;
        Node* parent = nullptr;
        Color color = RED;

        Node(const Key& k, const Value& v) : key(k), value(v) {}
    };

    using NodeType = Node;

    NodeType* root = nullptr;      ///< 根节点 / Root node
    std::size_t size_ = 0;         ///< 节点数量 / Number of nodes

    // ---------- 红黑树内部操作 / Red-black tree internal operations ----------

    // 左旋 / Left rotation
    inline void rotate_left(NodeType* x) {
        NodeType* y = x->right;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent) root = y;
        else if (x == x->parent->left) x->parent->left = y;
        else x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    // 右旋 / Right rotation
    inline void rotate_right(NodeType* x) {
        NodeType* y = x->left;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (!x->parent) root = y;
        else if (x == x->parent->right) x->parent->right = y;
        else x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    // 插入修复 / Fix properties after insertion
    inline void fix_insert(NodeType* z) {
        while (z->parent && z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                NodeType* y = z->parent->parent->right;
                if (y && y->color == RED) {
                    // Case 1: 叔叔为红色 / Uncle is red
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        // Case 2: 内旋转 / Inner rotation
                        z = z->parent;
                        rotate_left(z);
                    }
                    // Case 3: 外旋转 / Outer rotation
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rotate_right(z->parent->parent);
                }
            } else {
                NodeType* y = z->parent->parent->left;
                if (y && y->color == RED) {
                    z->parent->color = BLACK;
                    y->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotate_right(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    rotate_left(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    // 删除修复 / Fix properties after deletion
    inline void fix_erase(NodeType* x, NodeType* x_parent) {
        while (x != root && (!x || x->color == BLACK)) {
            if (x == x_parent->left) {
                NodeType* w = x_parent->right;
                if (w && w->color == RED) {
                    // Case 1: 兄弟为红色 / Sibling is red
                    w->color = BLACK;
                    x_parent->color = RED;
                    rotate_left(x_parent);
                    w = x_parent->right;
                }
                if ((!w->left || w->left->color == BLACK) && (!w->right || w->right->color == BLACK)) {
                    // Case 2: 两个子节点都是黑色 / Both children black
                    w->color = RED;
                    x = x_parent;
                    x_parent = x->parent;
                } else {
                    if (!w->right || w->right->color == BLACK) {
                        if (w->left) w->left->color = BLACK;
                        w->color = RED;
                        rotate_right(w);
                        w = x_parent->right;
                    }
                    // Case 3: 修复并旋转 / Fix and rotate
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->right) w->right->color = BLACK;
                    rotate_left(x_parent);
                    x = root;
                }
            } else {
                NodeType* w = x_parent->left;
                if (w && w->color == RED) {
                    w->color = BLACK;
                    x_parent->color = RED;
                    rotate_right(x_parent);
                    w = x_parent->left;
                }
                if ((!w->right || w->right->color == BLACK) && (!w->left || w->left->color == BLACK)) {
                    w->color = RED;
                    x = x_parent;
                    x_parent = x->parent;
                } else {
                    if (!w->left || w->left->color == BLACK) {
                        if (w->right) w->right->color = BLACK;
                        w->color = RED;
                        rotate_left(w);
                        w = x_parent->left;
                    }
                    w->color = x_parent->color;
                    x_parent->color = BLACK;
                    if (w->left) w->left->color = BLACK;
                    rotate_right(x_parent);
                    x = root;
                }
            }
        }
        if (x) x->color = BLACK;
    }

    // 查找最小节点 / Find minimum node
    NodeType* minimum(NodeType* node) const {
        while (node->left) node = node->left;
        return node;
    }

    // 子树替换 / Subtree transplant
    inline void transplant(NodeType* u, NodeType* v) {
        if (!u->parent) root = v;
        else if (u == u->parent->left) u->parent->left = v;
        else u->parent->right = v;
        if (v) v->parent = u->parent;
    }

    // 清空节点 / Clear all nodes
    void clear(NodeType* node) {
        if (!node) return;
        clear(node->left);
        clear(node->right);
        node->recycle();
    }

    // 内部递归遍历 / Internal inorder traversal
    template <typename Func>
    void inorder_traverse(NodeType* node, Func&& func) {
        if (!node) return;
        inorder_traverse(node->left, func);
        func(node->key, node->value);
        inorder_traverse(node->right, func);
    }
};
