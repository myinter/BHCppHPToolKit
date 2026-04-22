# BHCppHPToolKit

`BHCppHPToolKit` is a header-only C++ toolkit for performance-sensitive systems.

`BHCppHPToolKit` 是一个面向性能敏感场景的 `header-only` C++ 工具库。

It focuses on:
- pooled containers
- page-aware memory utilities
- lightweight synchronization primitives
- practical building blocks for cache systems, task pipelines, and multithreaded services

它主要聚焦在：
- 池化容器
- 感知系统页大小的内存工具
- 轻量同步原语
- 面向缓存系统、任务流水线和多线程服务的基础组件

## Advantages | 优势

- `Header-only`: easy to integrate, easy to distribute, minimal linking overhead.  
  `Header-only`：接入简单，分发方便，减少链接层复杂度。

- `C++11 compatible`: suitable for older production codebases and legacy toolchains.  
  兼容 `C++11`：适合老项目、旧编译器环境和稳定型工程代码库。

- `Performance-oriented`: reduces repeated allocation, improves locality, and avoids unnecessary heavyweight abstractions.  
  面向性能：减少频繁分配释放，提升内存局部性，尽量避免过重抽象带来的额外开销。

- `Practical concurrency tools`: provides spin-based locks, read-write locks, semaphores, and queue-style task scheduling.  
  实用并发工具：提供自旋锁、读写锁、信号量和队列式任务调度能力。

- `Composable modules`: memory pool, containers, and synchronization modules can be used independently.  
  模块可组合：内存池、容器、同步模块可以独立使用，也可以组合构建更复杂的数据结构。

## Modules | 模块

- `Foudations/MathBits.hpp`  
  Common math and bit helpers.  
  通用数学与位运算辅助函数。

- `MultiPlatforms/PlatformMemory.hpp`  
  Cross-platform page size helpers.  
  跨平台内存页大小相关工具。

- `Memory/SegmentedObjectPool.hpp`  
  Segmented object pool for pooled node/object allocation.  
  分段对象池，适合池化节点或对象分配。

- `Memory/BlockMemPool.hpp`  
  Page-based block memory pool with typed block views and reference counting.  
  基于页面粒度的块内存池，支持类型化块访问和引用计数。

- `HPContainer/PooledList.hpp`  
  Doubly linked pooled list.  
  双向池化链表。

- `HPContainer/PooledHashList.hpp`  
  List with index cache acceleration.  
  带索引缓存加速的链表。

- `HPContainer/PooledLinkedHashList.hpp`  
  Hash lookup plus linked order maintenance, suitable for LRU-style usage.  
  同时支持哈希访问和链表顺序维护，适合 LRU 类场景。

- `HPContainer/PooledHashMap.hpp`  
  Hash map backed by pooled block memory.  
  基于池化块内存实现的哈希映射。

- `HPContainer/PooledMap.hpp`  
  Ordered map based on a red-black tree.  
  基于红黑树的有序映射。

- `HPContainer/PooledVector.hpp`  
  Vector-like dynamic array backed by pooled block memory.  
  类似 `std::vector` 的池化动态数组。

- `HPContainer/SegVector.hpp`  
  Segmented vector for large-capacity growth scenarios.  
  适合大容量增长场景的分段向量。

- `MultiThreadAndMutex/BHSync.hpp`  
  Smart locks, read-write locks, semaphores, recursive locks, and queue-based task scheduling.  
  智能锁、读写锁、信号量、递归锁，以及队列式任务调度工具。

- `MultiThreadAndMutex/RWRingQueue.hpp`  
  Fixed-capacity ring queue built on `RWSmartLock`.  
  基于 `RWSmartLock` 的固定容量环形队列。

## Requirements | 依赖要求

- C++11 or newer  
  `C++11` 及以上

- A compiler with decent C++11 support  
  支持较完整 `C++11` 的编译器

- CMake 3.20+ for the packaged build/install flow  
  如果使用 CMake 安装流程，建议使用 `CMake 3.20+`

## Quick Start | 快速开始

### Include Directly | 直接包含

```cpp
#include <BHCppHPToolKit.hpp>
```

Or include individual modules:

或者按模块单独包含：

```cpp
#include <Memory/BlockMemPool.hpp>
#include <HPContainer/PooledVector.hpp>
#include <HPContainer/PooledLinkedHashList.hpp>
#include <MultiThreadAndMutex/BHSync.hpp>
```

### CMake Integration | CMake 接入

After install, use:

安装后可直接：

```cmake
find_package(BHCppHPToolKit CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE BHCppHPToolKit::BHCppHPToolKit)
```

## Build And Install | 构建与安装

Configure and build:

配置并构建：

```bash
cmake -S . -B build
cmake --build build
```

Install to a local prefix:

安装到本地目录：

```bash
cmake --install build --prefix ./install
```

## Example | 示例

```cpp
#include <BHCppHPToolKit.hpp>
#include <iostream>
#include <string>

int main() {
    PooledHashMap<std::string, int> scores;
    scores["alice"] = 95;
    scores["bob"] = 88;

    PooledLinkedHashList<std::string, std::string> lru;
    lru.insert_or_assign_back("user:1", "token-A");
    lru.insert_or_assign_back("user:2", "token-B");
    lru.touch("user:1");

    SmartLock lock;
    int counter = 0;
    lock.lock();
    ++counter;
    lock.unlock();

    std::cout << "alice = " << scores.at("alice") << std::endl;
    std::cout << "most recent key = " << lru.front().first << std::endl;
    std::cout << "counter = " << counter << std::endl;
    return 0;
}
```

For a larger runnable demo, see:

如果想看更完整、可直接运行的示例，请参考：

- [examples/find_package/main.cpp](./examples/find_package/main.cpp)

## Typical Use Cases | 典型使用场景

- in-memory cache systems  
  内存缓存系统

- LRU / ordered key-value storage  
  LRU / 有序键值存储

- low-latency service components  
  低延迟服务模块

- game server / realtime state containers  
  游戏服务端 / 实时状态容器

- multithreaded producer-consumer pipelines  
  多线程生产者消费者流水线

## License | 许可证

MIT
