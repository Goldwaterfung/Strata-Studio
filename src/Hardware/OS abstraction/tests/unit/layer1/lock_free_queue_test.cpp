// lock_free_queue_test.cpp
// Unit test for Layer 1 Lock-Free Queue

#include "../../../common/lock_free_queue.h"
#include <cassert>
#include <thread>
#include <vector>
#include <iostream>

using namespace Layer1;

void testBasicPushPop() {
    auto queue = LockFreeQueueFactory<int>::create(1024);
    assert(queue != nullptr);

    assert(queue->isEmpty());
    assert(queue->push(1));
    assert(queue->push(2));
    assert(!queue->isEmpty());

    int val;
    assert(queue->pop(val) && val == 1);
    assert(queue->pop(val) && val == 2);
    assert(queue->isEmpty());
    
    std::cout << "Basic Push/Pop passed." << std::endl;
}

void testSPSC() {
    auto queue = LockFreeQueueFactory<int>::create(1024);
    const int count = 100000;
    
    std::thread producer([&]() {
        for (int i = 0; i < count; ++i) {
            while (!queue->push(i)) { std::this_thread::yield(); }
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < count; ++i) {
            int val;
            while (!queue->pop(val)) { std::this_thread::yield(); }
            assert(val == i);
        }
    });

    producer.join();
    consumer.join();
    
    std::cout << "SPSC test passed." << std::endl;
}

int main() {
    testBasicPushPop();
    testSPSC();
    return 0;
}
