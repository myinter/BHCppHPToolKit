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

bool demo_sync_primitives() {
    print_section("BHSync Locks");

    SmartLock counterLock;
    std::int64_t sharedCounter = 0;
    std::vector<std::thread> workers;

    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&]() {
            for (int loop = 0; loop < 5000; ++loop) {
                counterLock.lock();
                ++sharedCounter;
                counterLock.unlock();
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    std::cout << "SmartLock protected counter: " << sharedCounter << std::endl;

    RWSmartLock rwLock;
    int sharedValue = 5;
    int readSnapshot = 0;

    std::thread reader([&]() {
        rwLock.readLock();
        readSnapshot = sharedValue;
        rwLock.readUnlock();
    });

    std::thread writer([&]() {
        rwLock.writeLock();
        sharedValue += 10;
        rwLock.writeUnlock();
    });

    reader.join();
    writer.join();

    RecursiveSmartLock recursiveLock;
    int recursiveCounter = 0;
    recursive_lock_demo(recursiveLock, 2, recursiveCounter);

    AtomicSemaphore semaphore(0);
    std::thread notifier([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        semaphore.release();
    });
    semaphore.acquire();
    notifier.join();

    std::cout << "RWSmartLock read snapshot: " << readSnapshot
              << ", final shared value: " << sharedValue
              << ", RecursiveSmartLock count: " << recursiveCounter << std::endl;

    return sharedCounter == 20000 && sharedValue == 15 && recursiveCounter == 3;
}

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
    const bool syncOk = demo_sync_primitives();
    const bool tasksOk = demo_task_dependencies();

    const bool allOk = containersOk && syncOk && tasksOk;
    std::cout << "\nExample result: " << (allOk ? "PASS" : "FAIL") << std::endl;
    return allOk ? 0 : 1;
}
