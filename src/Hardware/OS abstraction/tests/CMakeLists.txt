# tests/CMakeLists.txt
# Layer 1: Hardware/OS Abstraction Tests

cmake_minimum_required(VERSION 3.20)

# unit tests
add_executable(lock_free_queue_test unit/layer1/lock_free_queue_test.cpp)
target_link_libraries(lock_free_queue_test PRIVATE layer1_common)
add_test(NAME LockFreeQueueTest COMMAND lock_free_queue_test)
