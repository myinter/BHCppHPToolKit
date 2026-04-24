// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Bighiung
/*
 * PooledList.hpp
 *
 * PooledList - 池化链表模板类
 *
 * 功能 / Features:
 * 1. 基于 SegmentedObjectPool 实现节点池化 / Node allocation is managed via SegmentedObjectPool.
 * 2. 提供 push_back、push_front、insert、erase、swap 等链表操作 / Supports push_back, push_front, insert, erase, swap operations.
 * 3. 支持通过闭包迭代访问节点 / Allows iteration over elements via lambda/closure functions.
 * 4. 支持 Hash 索引加速的随机访问 / Provides hash-indexed fast access via operator[].
 * 5. 可通过 std::move 将另一个同类型 PooledList 插入指定位置 / Supports move-insertion of another PooledList.
 * 6. 节点在插入/删除/交换时自动更新索引，提高性能 / Updates hash index efficiently on modifications.
 * 7. 适用于性能敏感场景，如游戏、即时通信和高频交易 / Suitable for performance-critical applications like games, IM, HFT.
 */

#pragma once
#include "../Memory/SegmentedObjectPool.hpp"
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <cstddef>
#include <utility>

template <typename T>
class PooledList {
    
    
public:
    struct LinkedListNode : public PooledObject<LinkedListNode> {
        T value;
        LinkedListNode* prev = nullptr;
        LinkedListNode* next = nullptr;

        template <typename... Args>
        LinkedListNode(Args&&... args) : value(std::forward<Args>(args)...) {}
        void reset() override { prev = next = nullptr; }
    };

    PooledList() = default;
    ~PooledList() { clear(); }

    bool empty() const noexcept { return _size == 0; }
    std::size_t size() const noexcept { return _size; }

    T& front() { if (!_head) throw std::out_of_range("PooledList is empty"); return _head->value; }
    T& back()  { if (!_tail) throw std::out_of_range("PooledList is empty"); return _tail->value; }

    
    
    // -------- 插入 --------
    template <typename... Args>
    void push_back(Args&&... args) {
        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
        if (!_head) {
            _head = _tail = n;
        } else {
            _tail->next = n;
            n->prev = _tail;
            _tail = n;
        }
        ++_size;
    }

    template <typename... Args>
    void push_front(Args&&... args) {
        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
        if (!_head) {
            _head = _tail = n;
        } else {
            n->next = _head;
            _head->prev = n;
            _head = n;
        }
        ++_size;
    }

    // -------- 插入指定值 --------
    void insert(std::size_t pos, const T& value) {
        if (pos > _size) throw std::out_of_range("PooledList insert position out of range");
        LinkedListNode* n = LinkedListNode::create(value);
        if (pos == 0) {
            n->next = _head;
            if (_head) _head->prev = n;
            _head = n;
            if (!_tail) _tail = n;
        } else if (pos == _size) {
            n->prev = _tail;
            if (_tail) _tail->next = n;
            _tail = n;
        } else {
            LinkedListNode* cur = nodeAt(pos);

            LinkedListNode* prev = cur->prev;
            prev->next = n;
            n->prev = prev;
            n->next = cur;
            cur->prev = n;
        }
        ++_size;
    }

    // -------- 插入另一个 PooledList (move) --------
    void insert_list(std::size_t pos, PooledList&& other) {
        if (pos > _size) throw std::out_of_range("PooledList insert position out of range");
        if (other.empty()) return;

        if (pos == 0) {
            other._tail->next = _head;
            if (_head) _head->prev = other._tail;
            _head = other._head;
            if (!_tail) _tail = other._tail;
        } else if (pos == _size) {
            _tail->next = other._head;
            other._head->prev = _tail;
            _tail = other._tail;
        } else {

            LinkedListNode* cur = nodeAt(pos);

            LinkedListNode* prev = cur->prev;
            prev->next = other._head;
            other._head->prev = prev;
            other._tail->next = cur;
            cur->prev = other._tail;
        }

        _size += other._size;
        other._head = other._tail = nullptr;
        other._size = 0;
    }

    LinkedListNode *nodeAt(std::size_t idx) {

        if (idx >= _size) {
            return nullptr;
        }

        // 根据idx选择从 _head还是_tail 进行查找。
        LinkedListNode *startNode;
        size_t startIdx;
        size_t distance;

        size_t midIdx = (_size >> 1);

        if (_size > 16 && idx > midIdx) {
            // 从尾部节点开始检索更快！
            startIdx = _size - 1;
            startNode = _tail;
            distance = (startIdx - idx);
        } else {
            startIdx = 0;
            startNode = _head;
            distance = idx;
        }

        LinkedListNode* cur = startNode;

        if (startIdx < idx) {
            // 需要从start往尾部移动

            while (distance) {
                cur = cur->next;
                distance--;
            }

        } else {
            // 需要从start往头部移动
            
            while (distance) {
                cur = cur->prev;
                distance--;
            }

        }

        return cur;

    }

    void erase(std::size_t idx) {

        LinkedListNode* cur = nodeAt(idx);

        erase(cur);
    }

    void erase(LinkedListNode* n) {
        if (!n) return;
        LinkedListNode* prev = n->prev;
        LinkedListNode* next = n->next;

        if (prev) prev->next = next; else _head = next;
        if (next) next->prev = prev; else _tail = prev;

        n->recycle();
        --_size;

    }

    void pop_front() { if (_head) erase(_head); }
    void pop_back()  { if (_tail) erase(_tail); }

    void clear() {
        LinkedListNode* cur = _head;
        while (cur) {
            LinkedListNode* next = cur->next;
            cur->recycle();
            cur = next;
        }
        _head = _tail = nullptr;
        _size = 0;
    }

    template <typename Func>
    void for_each(Func&& fn) {
        LinkedListNode* cur = _head;
        while (cur) {
            fn(cur->value);
            cur = cur->next;
        }
    }

    long long for_each() {
        LinkedListNode* cur = _head;
        long long sum = 0;
        while (cur) {
            sum += cur->value;
            cur = cur->next;
        }
        return sum;
    }

    void for_each_reverse(const std::function<void(T&)>& fn) {
        LinkedListNode* cur = _tail;
        while (cur) {
            fn(cur->value);
            cur = cur->prev;
        }
    }

    void for_each_const(const std::function<void(const T&)>& fn) const {
        LinkedListNode* cur = _head;
        while (cur) {
            fn(cur->value);
            cur = cur->next;
        }
    }

private:

    LinkedListNode* _head = nullptr;
    LinkedListNode* _tail = nullptr;

    std::size_t _size = 0;


};

template <typename K, typename V, typename Pred>
void erase_if(std::unordered_map<K, V>& umap, Pred pred) {
    for (auto it = umap.begin(); it != umap.end(); ) {
        if (pred(it->first, it->second)) {
            it = umap.erase(it);  // 删除并获取下一个迭代器
        } else {
            ++it;
        }
    }
}

template <typename T>
class PooledHashList {
private:
    struct LinkedListNode : public PooledObject<LinkedListNode> {
        T value;
        LinkedListNode* prev = nullptr;
        LinkedListNode* next = nullptr;

        template <typename... Args>
        LinkedListNode(Args&&... args) : value(std::forward<Args>(args)...) {}
        void reset() override { prev = next = nullptr; }
    };

    LinkedListNode* _head = nullptr;
    LinkedListNode* _tail = nullptr;

    std::unordered_map<size_t, LinkedListNode *> _indexCache;

    std::size_t _size = 0;

    std::size_t _cacheMaxSize = 0;

public:
    PooledHashList(size_t cacheMaxSize = 128): _cacheMaxSize(cacheMaxSize)  {
        _indexCache.rehash(cacheMaxSize);
    }

    ~PooledHashList() { clear(); }

    bool empty() const noexcept { return _size == 0; }
    std::size_t size() const noexcept { return _size; }

    T& front() { if (!_head) throw std::out_of_range("PooledList is empty"); return _head->value; }
    T& back()  { if (!_tail) throw std::out_of_range("PooledList is empty"); return _tail->value; }

    // -------- 插入 --------
    template <typename... Args>
    void push_back(Args&&... args) {
        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
        if (!_head) {
            _head = _tail = n;
        } else {
            _tail->next = n;
            n->prev = _tail;
            _tail = n;
        }

        if (_indexCache.size() < _cacheMaxSize) {

            if (_size < 4096) {
                // 尺寸小于 每间64的数据，增加一个缓存节点，加快节点的检索速速
                if ((_size & 63) == 0) {
                    _indexCache[_size] = n;
                }
            } else if (_size & (255)) {
                _indexCache[_size] = n;
            }

        }
        
        ++_size;

    }

    template <typename... Args>
    void push_front(Args&&... args) {
        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
        if (!_head) {
            _head = _tail = n;
        } else {
            n->next = _head;
            _head->prev = n;
            _head = n;
        }
        ++_size;

        // 删除全部缓存
        _indexCache.clear();
    }

    // -------- 插入指定值 --------
    void insert(std::size_t pos, const T& value) {
        if (pos > _size) throw std::out_of_range("PooledList insert position out of range");
        if (pos == 0) {
            push_front(value);
        } else if (pos == _size) {
            push_back(value);
        } else {
            LinkedListNode* cur = nodeAt(pos);
            LinkedListNode* n = LinkedListNode::create(value);
            LinkedListNode* prev = cur->prev;
            prev->next = n;
            n->prev = prev;
            n->next = cur;
            cur->prev = n;

            // 清除原有的大于pos的缓存
            deleteCacheUpperThan(pos);

            // 将当前节点插入索引缓存,加快随机访问的速度
            if (pos > 16 || (_size - pos) > 16) {
                _indexCache[pos] = n;
            }

        }
        ++_size;

    }

    // -------- 插入另一个 PooledList (move) --------
    void insert_list(std::size_t pos, PooledHashList&& other) {
        if (pos > _size) throw std::out_of_range("PooledList insert position out of range");
        if (other.empty()) return;

        if (pos == 0) {
            other._tail->next = _head;
            if (_head) _head->prev = other._tail;
            _head = other._head;
            if (!_tail) _tail = other._tail;
        } else if (pos == _size) {
            _tail->next = other._head;
            other._head->prev = _tail;
            _tail = other._tail;
            _indexCache.clear();
        } else {

            LinkedListNode* cur = nodeAt(pos);

            LinkedListNode* prev = cur->prev;
            prev->next = other._head;
            other._head->prev = prev;
            other._tail->next = cur;
            cur->prev = other._tail;

            deleteCacheUpperThan(pos);

            // 将插入的列表的头和尾部，加入索引缓存
            size_t otherHeadIdx = pos;
            size_t distance = other._size - 1;
            size_t otherTailIdx = (pos + distance);

            _indexCache[otherHeadIdx] = other._head;

            if (distance > 64) {
                // 尾部也添加索引缓存
                _indexCache[otherTailIdx] = other._tail;
            }

        }

        _size += other._size;
        other._head = other._tail = nullptr;
        other._size = 0;
        other._indexCache.clear();
    }
    
    void deleteCacheUpperThan(size_t idx) {

        erase_if(_indexCache, [idx](size_t key, LinkedListNode* node){
            return key >= idx;
        });

    }

    LinkedListNode *nodeAt(std::size_t idx) {

        if (idx >= _size) {
            return nullptr;
        }

        // 从缓存中检索idx对应节点
        typename std::unordered_map<size_t, LinkedListNode *>::iterator cacheIt = _indexCache.find(idx);
        if (cacheIt != _indexCache.end()) {
            return cacheIt->second;
        }

        // 找到距离idx最近的已经缓存的节点，作为遍历搜索的起点
        LinkedListNode *startNode = _head;
        size_t startIdx = 0;
        
        size_t currentCacheSize = _indexCache.size();
        
        size_t midIdx = (_size >> 1);

        if (idx > midIdx) {
            // 从尾部节点开始检索更快！
            startIdx = _size - 1;
            startNode = _tail;
        }

        size_t distance = 0;

        // 计算起点到idx的距离
        if (idx > startIdx) {
            distance = idx - startIdx;
        } else {
            distance = startIdx - idx;
        }

        if (currentCacheSize && (distance > 64)) {
            // 距离大于 64时，视图搜索距离更近的节点，减小搜索距离

            // 进行缓存检索，找到最近的节点
            for (auto &kv: _indexCache) {

                size_t index = kv.first;
                LinkedListNode *node = kv.second;
                size_t curDistance = 0;

                if (index > idx) {
                    curDistance = index - idx;
                } else {
                    curDistance = idx - index;
                }

                if (curDistance < distance) {
                    // 距离目标idx更小，或者已经小于64，则停止
                    startNode = node;
                    startIdx = index;
                    distance = curDistance;
                    if (distance < 64) {
                        // 为了提高效率，一旦距离小于64，就不继续搜索，直接从这个节点开始动作
                        break;
                    }
                }

            }
        }

        LinkedListNode* cur = startNode;

        if (startIdx < idx) {
            // 需要从start往尾部移动

            while (distance) {
                cur = cur->next;
                distance--;
            }

        } else {
            // 需要从start往头部移动
            
            while (distance) {
                cur = cur->prev;
                distance--;
            }

        }

        // 将检索到的节点缓存（当缓存数量少于 _cacheMaxSize 或者少于 _size / 128 的时候）。
        if (currentCacheSize < _cacheMaxSize || (currentCacheSize < (_size >> 7))) {
            _indexCache[idx] = cur;
        }

        return cur;
    }

    void erase(std::size_t idx) {
        
        if (idx == 0) {
            pop_front();
            return;
        }

        if (idx + 1 == _size) {
            pop_back();
            return;
        }

        LinkedListNode *cur = nodeAt(idx);
        LinkedListNode *next = cur->next;

        // 删除节点
        erase(cur);

        // 删除大于idx位置的缓存节点信息。
        deleteCacheUpperThan(idx);

        if (next) {
            // 添加缓存
            _indexCache[idx] = next;
        }

    }

    void erase(LinkedListNode* n) {
        if (!n) return;
        LinkedListNode* prev = n->prev;
        LinkedListNode* next = n->next;

        if (prev) prev->next = next; else _head = next;
        if (next) next->prev = prev; else _tail = prev;

        n->recycle();
        --_size;

    }

    void pop_front() {
        if (_head)
            erase(_head);
        _indexCache.clear();
    }

    void pop_back()  {

        // 删除尾部节点的缓存
        _indexCache.erase(_size - 1);

        if (_tail)
            erase(_tail);

    }

    void clear() {
        LinkedListNode* cur = _head;
        while (cur) {
            LinkedListNode* next = cur->next;
            cur->recycle();
            cur = next;
        }
        _head = _tail = nullptr;
        _size = 0;
    }

    void for_each(const std::function<void(T&)>& fn) {
        LinkedListNode* cur = _head;
        while (cur) {
            fn(cur->value);
            cur = cur->next;
        }
    }

    void for_each_reverse(const std::function<void(T&)>& fn) {
        LinkedListNode* cur = _tail;
        while (cur) {
            fn(cur->value);
            cur = cur->prev;
        }
    }

    void for_each_const(const std::function<void(const T&)>& fn) const {
        LinkedListNode* cur = _head;
        while (cur) {
            fn(cur->value);
            cur = cur->next;
        }
    }

    T& operator[](std::size_t idx) {
        return nodeAt(idx)->value;
    }

    T& at(std::size_t idx) { return operator[](idx); }

};

//template <typename T>
//class PooledHashList {
//private:
//    struct LinkedListNode : public PooledObject<LinkedListNode> {
//        T value;
//        LinkedListNode* prev = nullptr;
//        LinkedListNode* next = nullptr;
//
//        template <typename... Args>
//        LinkedListNode(Args&&... args) : value(std::forward<Args>(args)...) {}
//        void reset() override { prev = next = nullptr; }
//    };
//
//    LinkedListNode* _head = nullptr;
//    LinkedListNode* _tail = nullptr;
//    std::size_t _size = 0;
//
//    std::vector<LinkedListNode*> _indexArray;
//    std::size_t _indexValidFrom = 0; // 从哪个索引开始无效
//
//    void ensure_index() {
//        if (_indexValidFrom >= _size) return;
//        if (_indexArray.size() < _size) _indexArray.resize(_size);
//
//        LinkedListNode* cur = nullptr;
//        std::size_t start = _indexValidFrom;
//
//        if (start == 0) {
//            cur = _head;
//        } else {
//            cur = _indexArray[start - 1]->next;
//        }
//
//        for (std::size_t i = start; i < _size; ++i) {
//            _indexArray[i] = cur;
//            cur = cur->next;
//        }
//        _indexValidFrom = _size;
//    }
//
//public:
//    PooledHashList() = default;
//    ~PooledHashList() { clear(); }
//
//    bool empty() const noexcept { return _size == 0; }
//    std::size_t size() const noexcept { return _size; }
//
//    T& front() { if (!_head) throw std::out_of_range("PooledHashList is empty"); ensure_index(); return _indexArray[0]->value; }
//    T& back()  { if (!_tail) throw std::out_of_range("PooledHashList is empty"); ensure_index(); return _indexArray[_size - 1]->value; }
//
//    template <typename... Args>
//    void push_back(Args&&... args) {
//        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
//        if (!_head) {
//            _head = _tail = n;
//            _indexValidFrom = 0;
//        } else {
//            _tail->next = n;
//            n->prev = _tail;
//            _tail = n;
//            if (_indexValidFrom == _size)
//                _indexValidFrom = _size;
//        }
//        ++_size;
//    }
//
//    template <typename... Args>
//    void push_front(Args&&... args) {
//        LinkedListNode* n = LinkedListNode::create(std::forward<Args>(args)...);
//        if (!_head) {
//            _head = _tail = n;
//            _indexValidFrom = 1;
//            // 加入第一个索引
//            _indexArray.push_back(n);
//        } else {
//            n->next = _head;
//            _head->prev = n;
//            _head = n;
//            // 索引处于有效状态，直接加入
//            if (_indexValidFrom >= _size) {
//                _indexValidFrom = _size + 1;
//                _indexArray.push_back(n);
//            }
//        }
//        ++_size;
//    }
//
//    void insert(std::size_t pos, const T& value) {
//        if (pos > _size) throw std::out_of_range("PooledHashList insert position out of range");
//        LinkedListNode* n = LinkedListNode::create(value);
//
//        if (!_head) {
//            _head = _tail = n;
//            _indexValidFrom = 0;
//        } else if (pos == 0) {
//            n->next = _head;
//            _head->prev = n;
//            _head = n;
//            _indexValidFrom = 0;
//        } else if (pos == _size) {
//            _tail->next = n;
//            n->prev = _tail;
//            _tail = n;
//            _indexValidFrom = _size;
//            _indexArray.push_back(n);
//        } else {
//            LinkedListNode* cur = at(pos);
//            LinkedListNode* prev = cur->prev;
//
//            prev->next = n;
//            n->prev = prev;
//            n->next = cur;
//            cur->prev = n;
//
//            _indexValidFrom = std::min(_indexValidFrom, pos);
//        }
//        ++_size;
//    }
//
//    void insert_list(std::size_t pos, PooledHashList&& other) {
//        if (other.empty()) return;
//        if (pos > _size) throw std::out_of_range("PooledHashList insert_list position out of range");
//
//        if (!_head) {
//            _head = other._head;
//            _tail = other._tail;
//            _size = other._size;
//            _indexValidFrom = 0;
//        } else if (pos == 0) {
//            other._tail->next = _head;
//            _head->prev = other._tail;
//            _head = other._head;
//            _indexValidFrom = 0;
//            _size += other._size;
//        } else if (pos == _size) {
//            _tail->next = other._head;
//            other._head->prev = _tail;
//            _tail = other._tail;
//            _indexValidFrom = _size;
//            _size += other._size;
//        } else {
//            ensure_index();
//            LinkedListNode* cur = _indexArray[pos];
//            LinkedListNode* prev = cur->prev;
//
//            prev->next = other._head;
//            other._head->prev = prev;
//            other._tail->next = cur;
//            cur->prev = other._tail;
//
//            _indexValidFrom = std::min(_indexValidFrom, pos);
//            _size += other._size;
//        }
//
//        other._head = other._tail = nullptr;
//        other._size = 0;
//        other._indexValidFrom = 0;
//        other._indexArray.clear();
//    }
//
//    void erase(std::size_t idx) {
//        if (idx >= _size) return;
//        ensure_index();
//        LinkedListNode* n = _indexArray[idx];
//
//        if (n->prev) n->prev->next = n->next;
//        else _head = n->next;
//
//        if (n->next) n->next->prev = n->prev;
//        else _tail = n->prev;
//
//        n->recycle();
//        --_size;
//        _indexValidFrom = std::min(_indexValidFrom, idx);
//    }
//
//    void pop_front() { if (_head) erase(0); }
//    void pop_back()  { if (_tail) erase(_size - 1); }
//
//    void clear() {
//        LinkedListNode* cur = _head;
//        while (cur) {
//            LinkedListNode* next = cur->next;
//            cur->recycle();
//            cur = next;
//        }
//        _head = _tail = nullptr;
//        _size = 0;
//        _indexArray.clear();
//        _indexValidFrom = 0;
//    }
//
//    void for_each(const std::function<void(T&)>& fn) {
//        LinkedListNode* cur = _head;
//        while (cur) {
//            fn(cur->value);
//            cur = cur->next;
//        }
//    }
//
//    void for_each_reverse(const std::function<void(T&)>& fn) {
//        ensure_index();
//        for (auto it = _indexArray.rbegin(); it != _indexArray.rend(); ++it) {
//            fn((*it)->value);
//        }
//    }
//
//    inline LinkedListNode *nodeAt(std::size_t idx) {
//        ensure_index();
//        return _indexArray[idx];
//    }
//
//    T& operator[](std::size_t idx) {
//        return nodeAt(idx);
//    }
//
//    T& at(std::size_t idx) { return operator[](idx); }
//};
