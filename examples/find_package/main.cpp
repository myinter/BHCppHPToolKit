#include <BHCppHPToolKit.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// 将CPU核心类型转换为字符串 / Convert CPU core type to string
const char* cpu_core_type_name(multi_platforms::CPUCoreType type) {
    switch (type) {
        case multi_platforms::CPUCoreType::Big: return "Big";
        case multi_platforms::CPUCoreType::Medium: return "Medium";
        case multi_platforms::CPUCoreType::Little: return "Little";
        case multi_platforms::CPUCoreType::HyperThread: return "HyperThread";
        case multi_platforms::CPUCoreType::Unknown:
        default:
            return "Unknown";
    }
}

// 递归锁示例 / Recursive lock demo
void recursive_lock_demo(RecursiveSmartLock& lock, int depth, int& counter) {
    lock.lock();
    ++counter;
    if (depth > 0) {
        recursive_lock_demo(lock, depth - 1, counter);
    }
    lock.unlock();
}

void print_section(const std::string& title) {
    std::cout << "\n==== " << title << " ====" << std::endl;
}

// 示例对象池使用的消息结构体 / Message struct used for object pool demo
struct DemoRawMessage : public PooledObject<DemoRawMessage> {
    std::string text;

    DemoRawMessage() {}
    explicit DemoRawMessage(const std::string& value) : text(value) {}

    void reset() {
        text.clear();
    }
};

// 示例对象池使用的共享消息结构体 / Shared message struct used for object pool demo
struct DemoSharedMessage : public RefCountedPooledObject<DemoSharedMessage> {
    std::string text;

    DemoSharedMessage() {}
    explicit DemoSharedMessage(const std::string& value) : text(value) {}

    void reset() {
        text.clear();
    }
};

bool demo_containers() {
    print_section("Containers");

    PooledList<std::string> messages;
    messages.push_back("world");
    messages.push_front("hello");
    std::cout << "PooledList front/back: " << messages.front() << " / " << messages.back() << std::endl;

    PooledVector<int> numbers;
    numbers.push_back(1);
    numbers.push_back(2);
    numbers.emplace_back(3);
    numbers.insert(numbers.begin() + 1, 99);
    numbers.erase(numbers.begin() + 1);
    std::cout << "PooledVector size: " << numbers.size() << ", back: " << numbers.back() << std::endl;

    PooledHashMap<std::string, int> scores;
    scores["alice"] = 10;
    scores.try_emplace("bob", 20);
    scores.emplace("carol", 30);
    std::cout << "PooledHashMap contains bob: " << std::boolalpha << scores.contains("bob")
              << ", carol score: " << scores.at("carol") << std::endl;

    PooledMap<int, std::string> ordered;
    ordered[2] = "two";
    ordered[1] = "one";
    std::cout << "PooledMap find(2): " << ordered.find(2) << std::endl;

    PooledLinkedHashList<std::string, std::string> lruOrder;
    lruOrder.insert_or_assign_back("user:1", "token-A");
    lruOrder.insert_or_assign_back("user:2", "token-B");
    lruOrder.touch("user:1");
    std::cout << "PooledLinkedHashList most recent key: " << lruOrder.front().first
              << ", least recent key: " << lruOrder.back().first << std::endl;

    RWRingQueue<std::string> ring(2, false, true);
    ring.emplace("msg-1");
    ring.emplace("msg-2");
    ring.emplace("msg-3");
    std::string popped;
    const bool poppedOk = ring.try_pop(popped);
    std::cout << "RWRingQueue pop: " << (poppedOk ? popped : std::string("<empty>"))
              << ", capacity after grow: " << ring.capacity() << std::endl;

    BlockMemPool pool;
    auto block = pool.allocateAs<uint32_t>(4, true);
    block[0] = 7;
    block[1] = 11;
    std::cout << "BlockMemPool values: [" << block[0] << ", " << block[1]
              << "], bytes: " << block.byte_size() << std::endl;

    return messages.size() == 2
        && numbers.size() == 3
        && scores.at("alice") == 10
        && lruOrder.front().first == "user:1"
        && poppedOk
        && popped == "msg-1"
        && ring.capacity() >= 3
        && block[1] == 11;
}

// 示例对象池演示 / Demo for object pool
bool demo_object_pool() {
    print_section("SegmentedObjectPool");

    DemoRawMessage* raw = DemoRawMessage::create("raw-message");
    std::cout << "PooledObject text: " << raw->text
              << " (manual recycle)" << std::endl;

    const std::size_t liveAfterRawCreate = SegmentedObjectPool<DemoRawMessage>::instance().live();
    raw->recycle();
    const std::size_t liveAfterRawRecycle = SegmentedObjectPool<DemoRawMessage>::instance().live();

    PooledSharedPtr<DemoSharedMessage> shared = DemoSharedMessage::make_shared("shared-message");
    PooledSharedPtr<DemoSharedMessage> sharedCopy = shared;
    PooledSharedPtr<DemoSharedMessage> sharedFromThis = shared->shared_from_this();
    const std::size_t sharedUseCount = shared.use_count();
    std::cout << "RefCountedPooledObject text: " << shared->text
              << ", use_count: " << sharedUseCount << std::endl;

    sharedCopy.reset();
    sharedFromThis.reset();
    const std::size_t liveBeforeFinalReset = SegmentedObjectPool<DemoSharedMessage>::instance().live();
    shared.reset();
    const std::size_t liveAfterFinalReset = SegmentedObjectPool<DemoSharedMessage>::instance().live();

    // 手动管理引用计数的示例 / Example of manually managing reference count
    DemoSharedMessage* manualPtr =
        SegmentedObjectPool<DemoSharedMessage>::instance().allocate("manual-ref-count");
    manualPtr->add_ref();
    const std::size_t manualUseCountAfterAddRef = manualPtr->use_count();
    std::cout << "Manual ref-count text: " << manualPtr->text
              << ", use_count after add_ref: " << manualUseCountAfterAddRef << std::endl;
    manualPtr->release_ref();
    const std::size_t liveAfterManualRelease = SegmentedObjectPool<DemoSharedMessage>::instance().live();

    // 手动释放的示例：
    DemoRawMessage* manualRaw =
        SegmentedObjectPool<DemoRawMessage>::instance().allocate("manual-raw-message");
    const std::size_t liveAfterManualRawAllocate = SegmentedObjectPool<DemoRawMessage>::instance().live();
    std::cout << "Manual pooled object text: " << manualRaw->text
              << " (allocate + recycle)" << std::endl;
    DemoRawMessage* recycledSlot = manualRaw;
    manualRaw->recycle();
    const std::size_t liveAfterManualRawRecycle = SegmentedObjectPool<DemoRawMessage>::instance().live();
    std::cout << "Recycled old address: " << static_cast<const void*>(recycledSlot) << std::endl;

    // 再次分配一个对象，看看是否重用了之前回收的槽位 / Allocate another object to see if it reuses the previously recycled slot
    DemoRawMessage* reusedRaw =
        SegmentedObjectPool<DemoRawMessage>::instance().allocate("reused-raw-message");
    const bool reusedSameAddress = (reusedRaw == recycledSlot);
    std::cout << "Reused pooled object text: " << reusedRaw->text
              << ", new address: " << static_cast<const void*>(reusedRaw)
              << ", same slot: " << reusedSameAddress << std::endl;
    reusedRaw->recycle();
    const std::size_t liveAfterReuseRecycle = SegmentedObjectPool<DemoRawMessage>::instance().live();

    return liveAfterRawCreate == 1
        && liveAfterRawRecycle == 0
        && sharedUseCount == 3
        && liveBeforeFinalReset == 1
        && liveAfterFinalReset == 0
        && manualUseCountAfterAddRef == 1
        && liveAfterManualRelease == 0
        && liveAfterManualRawAllocate == 1
        && liveAfterManualRawRecycle == 0
        && reusedSameAddress
        && liveAfterReuseRecycle == 0;
}

// 示例平台CPU信息演示 / Demo for platform thread information
bool demo_platform_cpu_info() {
    print_section("PlatformThread CPU Info");

    const std::vector<multi_platforms::CPUCoreInfo>& infos = multi_platforms::detectCPUCoreInfos();
    std::cout << "Detected CPU count: " << multi_platforms::getCPUCount() << std::endl;

    for (std::size_t i = 0; i < infos.size(); ++i) {
        std::cout << "CPU[" << infos[i].index << "] -> " << cpu_core_type_name(infos[i].type) << std::endl;
    }

    return !infos.empty()
        && multi_platforms::getCPUCount() == infos.size()
        && multi_platforms::getCPUCoreInfo(0) != NULL;
}

// 示例同步原语演示 / Demo for synchronization primitives
bool demo_sync_primitives() {
    print_section("BHSync Locks");

    BHGCD::BHGCDController queue(4, BHGCD::ThreadPriority::Normal);

    SmartLock counterLock;
    std::int64_t sharedCounter = 0;
    AtomicSemaphore counterDone(0);

    for (int i = 0; i < 4; ++i) {
        queue.enqueue([&]() {
            for (int loop = 0; loop < 5000; ++loop) {
                counterLock.lock();
                ++sharedCounter;
                counterLock.unlock();
            }
            counterDone.release();
        });
    }

    for (int i = 0; i < 4; ++i) {
        counterDone.acquire();
    }

    std::cout << "SmartLock protected counter: " << sharedCounter << std::endl;

    RWSmartLock rwLock;
    int sharedValue = 5;
    int readSnapshot = 0;
    AtomicSemaphore rwDone(0);

    queue.enqueue([&]() {
        rwLock.readLock();
        readSnapshot = sharedValue;
        rwLock.readUnlock();
        rwDone.release();
    });

    queue.enqueue([&]() {
        rwLock.writeLock();
        sharedValue += 10;
        rwLock.writeUnlock();
        rwDone.release();
    });

    rwDone.acquire();
    rwDone.acquire();

    RecursiveSmartLock recursiveLock;
    int recursiveCounter = 0;
    recursive_lock_demo(recursiveLock, 2, recursiveCounter);

    AtomicSemaphore semaphore(0);
    queue.enqueue([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        semaphore.release();
    });
    semaphore.acquire();

    std::cout << "RWSmartLock read snapshot: " << readSnapshot
              << ", final shared value: " << sharedValue
              << ", RecursiveSmartLock count: " << recursiveCounter << std::endl;

    return sharedCounter == 20000 && sharedValue == 15 && recursiveCounter == 3;
}

// 示例任务依赖演示 / Demo for task dependencies
bool demo_task_dependencies() {
    print_section("BHGCD Task Dependencies");

    auto* queue = new BHGCD::BHGCDController(3, BHGCD::ThreadPriority::Normal);
    AtomicSemaphore groupDone(0);
    AtomicSemaphore barrierDone(0);

    std::atomic<int> groupSum{0};
    std::atomic<int> normalAfterBarrier{0};

    const int groupId = queue->getGroupId();
    if (groupId < 0) {
        return false;
    }

    queue->enqueueGroup(groupId, [&]() { groupSum.fetch_add(1, std::memory_order_relaxed); });
    queue->enqueueGroup(groupId, [&]() { groupSum.fetch_add(2, std::memory_order_relaxed); });
    queue->enqueueGroup(groupId, [&]() { groupSum.fetch_add(3, std::memory_order_relaxed); });

    queue->setCallbackForGroup(groupId, [&]() {
        std::cout << "Group callback fired, sum = " << groupSum.load() << std::endl;
        groupDone.release();
    }, false);

    queue->fireGroup(groupId);
    groupDone.acquire();

    queue->enqueue([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        normalAfterBarrier.fetch_add(1, std::memory_order_relaxed);
    });

    queue->enqueueBarrier([&]() {
        std::cout << "Barrier task runs after previously executing normal tasks." << std::endl;
        barrierDone.release();
    });

    barrierDone.acquire();
    queue->enqueue([&]() {
        normalAfterBarrier.fetch_add(10, std::memory_order_relaxed);
        barrierDone.release();
    });
    barrierDone.acquire();

    std::cout << "Post-barrier accumulator: " << normalAfterBarrier.load() << std::endl;
    return groupSum.load() == 6 && normalAfterBarrier.load() == 11;
}

} // namespace

int main() {
    const bool containersOk = demo_containers();
    const bool objectPoolOk = demo_object_pool();
    const bool cpuInfoOk = demo_platform_cpu_info();
    const bool syncOk = demo_sync_primitives();
    const bool tasksOk = demo_task_dependencies();

    const bool allOk = containersOk && objectPoolOk && cpuInfoOk && syncOk && tasksOk;
    std::cout << "\nExample result: " << (allOk ? "PASS" : "FAIL") << std::endl;
    return allOk ? 0 : 1;
}
