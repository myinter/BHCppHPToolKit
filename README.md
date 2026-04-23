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

- `Object pooling with ownership options`: supports raw pooled objects for hot paths and reference-counted pooled smart pointers for safer lifetime management.  
  对象池支持多种生命周期管理方式：既支持面向极致性能的裸对象池，也支持带引用计数的池化智能指针，兼顾性能与易用性。

- `Practical concurrency tools`: provides spin-based locks, read-write locks, semaphores, and queue-style task scheduling.  
  实用并发工具：提供自旋锁、读写锁、信号量和队列式任务调度能力。

- `Cross-platform thread utilities`: exposes unified APIs for thread priority, CPU affinity, and lazy-loaded CPU topology inspection.  
  跨平台线程工具：统一封装线程优先级、CPU 绑定，以及懒加载的 CPU 拓扑与核心类型探测能力。

- `Composable modules`: memory pool, containers, and synchronization modules can be used independently.  
  模块可组合：内存池、容器、同步模块可以独立使用，也可以组合构建更复杂的数据结构。

## Modules | 模块

- `Foudations/MathBits.hpp`  
  Common math and bit helpers.  
  通用数学与位运算辅助函数。

- `MultiPlatforms/PlatformMemory.hpp`  
  Cross-platform page size helpers.  
  跨平台内存页大小相关工具。

- `MultiPlatforms/PlatformThread.hpp`  
  Cross-platform thread priority, CPU affinity, and cached CPU core info helpers.  
  跨平台线程优先级、CPU 绑定，以及带缓存的 CPU 核心信息工具。

- `Memory/SegmentedObjectPool.hpp`  
  Segmented object pool with `PooledObject`, `RefCountedPooledObject`, and pooled smart-pointer helpers.  
  分段对象池，提供 `PooledObject`、`RefCountedPooledObject` 以及池化智能指针辅助能力。

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

- `HPContainer/RWRingQueue.hpp`  
  Auto-expandable ring queue built on `RWSmartLock` and `BlockMemPool`.  
  基于 `RWSmartLock` 和 `BlockMemPool` 的可自动扩容环形队列。

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
#include <Memory/SegmentedObjectPool.hpp>
#include <HPContainer/PooledVector.hpp>
#include <HPContainer/PooledLinkedHashList.hpp>
#include <MultiPlatforms/PlatformThread.hpp>
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

`PlatformThread` also provides CPU and thread helpers:

`PlatformThread` 还提供了 CPU / 线程相关能力：

```cpp
#include <BHCppHPToolKit.hpp>
#include <iostream>
#include <thread>

int main() {
    const std::vector<multi_platforms::CPUCoreInfo>& cores =
        multi_platforms::getCPUCoreInfos();

    std::cout << "cpu count = " << multi_platforms::getCPUCount() << std::endl;
    if (!cores.empty()) {
        multi_platforms::bindCurrentThreadToCPU(cores.front().index);
    }

    std::thread worker([]() {});
    multi_platforms::setThreadPriority(worker, multi_platforms::ThreadPriority::High);
    if (cores.size() > 1) {
        multi_platforms::bindThreadToCPU(worker, cores[1].index);
    }
    worker.join();
    return 0;
}
```

The CPU info API is lazy-loaded and cached after the first query, and each entry reports a logical CPU index plus one of `Big`, `Medium`, `Little`, `HyperThread`, or `Unknown`.

CPU 信息 API 采用懒加载并在首次查询后缓存，每个条目都会返回逻辑 CPU 的 `index`，以及 `Big`、`Medium`、`Little`、`HyperThread`、`Unknown` 之一作为核心类型。

`SegmentedObjectPool` supports both manual recycle and reference-counted ownership:

`SegmentedObjectPool` 同时支持手动回收和引用计数两种对象生命周期管理方式：

```cpp
#include <BHCppHPToolKit.hpp>
#include <iostream>
#include <string>

struct RawMessage : public PooledObject<RawMessage> {
    std::string text;
    explicit RawMessage(const std::string& value) : text(value) {}
    void reset() { text.clear(); }
};

struct SharedMessage : public RefCountedPooledObject<SharedMessage> {
    std::string text;
    explicit SharedMessage(const std::string& value) : text(value) {}
    void reset() { text.clear(); }
};

int main() {
    RawMessage* raw = RawMessage::create("hot-path");
    std::cout << raw->text << std::endl;
    raw->recycle();

    PooledSharedPtr<SharedMessage> shared = SharedMessage::make_shared("safe-owner");
    PooledSharedPtr<SharedMessage> another = shared;
    std::cout << shared->text << ", use_count = " << shared.use_count() << std::endl;
    return 0;
}
```

Use `PooledObject<T>` when the caller fully controls recycle timing and wants the lowest overhead. Use `RefCountedPooledObject<T>` together with `PooledSharedPtr<T>` when object ownership is shared across modules or asynchronous tasks.

如果调用方可以完全控制回收时机、并希望拿到尽可能低的开销，可以使用 `PooledObject<T>`。如果对象所有权需要在多个模块或异步任务之间共享，则更适合使用 `RefCountedPooledObject<T>` 搭配 `PooledSharedPtr<T>`。

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

- pooled entity/message objects with explicit or shared ownership  
  具有显式回收或共享所有权的池化实体 / 消息对象

- multithreaded producer-consumer pipelines  
  多线程生产者消费者流水线

- thread scheduling experiments and CPU affinity tuning  
  线程调度实验与 CPU 亲和性调优

## License | 许可证

MIT
