#include <catch2/catch_test_macros.hpp>
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace Layer1;

class TestAsyncCallback : public IFileSystem::IAsyncCallback {
public:
    std::atomic<bool> completed{false};
    std::atomic<bool> failed{false};
    uint8_t* receivedData = nullptr;
    uint64_t bytesRead = 0;
    std::mutex mtx;
    std::condition_variable cv;

    ~TestAsyncCallback() {
        if (receivedData) {
            delete[] receivedData;
        }
    }

    void onSuccess(uint8_t* data, uint64_t bytesProcessed) override {
        std::lock_guard<std::mutex> lock(mtx);
        receivedData = data;
        bytesRead = bytesProcessed;
        completed = true;
        cv.notify_one();
    }

    void onError(int errorCode, const char* errorMessage) override {
        (void)errorCode;
        (void)errorMessage;
        std::lock_guard<std::mutex> lock(mtx);
        failed = true;
        completed = true;
        cv.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(2), [this] { return completed.load(); });
    }
};

TEST_CASE("Filesystem Async I/O", "[Layer1][Filesystem]") {
    auto fs = IFileSystem::create();
    REQUIRE(fs != nullptr);

    const char* testPath = "test_async.bin";
    const char* testContent = "Hello StrataStudio Async I/O!";
    uint64_t testSize = std::strlen(testContent);

    SECTION("Async Write then Async Read") {
        // 1. Async Write
        FileHandle hWrite = fs->openFile(testPath, false);
        REQUIRE(hWrite != INVALID_FILE_HANDLE);

        TestAsyncCallback writeCallback;
        uint8_t* writeBuffer = new uint8_t[testSize];
        std::memcpy(writeBuffer, testContent, testSize);

        OperationHandle opWrite = fs->writeFileAsync(hWrite, 0, writeBuffer, testSize, &writeCallback);
        REQUIRE(opWrite != INVALID_OPERATION_HANDLE);
        writeCallback.wait();
        
        REQUIRE(writeCallback.completed == true);
        REQUIRE(writeCallback.failed == false);
        fs->closeFile(hWrite);

        // 2. Async Read
        FileHandle hRead = fs->openFile(testPath, true);
        REQUIRE(hRead != INVALID_FILE_HANDLE);

        TestAsyncCallback readCallback;
        OperationHandle opRead = fs->readFileAsync(hRead, 0, testSize, &readCallback);
        REQUIRE(opRead != INVALID_OPERATION_HANDLE);
        readCallback.wait();

        REQUIRE(readCallback.completed == true);
        REQUIRE(readCallback.failed == false);
        REQUIRE(readCallback.bytesRead == testSize);
        REQUIRE(std::memcmp(readCallback.receivedData, testContent, testSize) == 0);

        fs->closeFile(hRead);
        
        // Cleanup
        // (fs doesn't have deleteFile yet, but we can assume it's fine)
    }
}

TEST_CASE("Filesystem Metadata", "[Layer1][Filesystem]") {
    auto fs = IFileSystem::create();
    REQUIRE(fs != nullptr);

    SECTION("Path Info and Exists") {
        FileInfo info;
        // "." should always exist and be a directory
        bool found = fs->getPathInfo(".", info);
        REQUIRE(found == true);
        REQUIRE(info.isDirectory == true);
        
        REQUIRE(fs->exists(".") == true);
        REQUIRE(fs->exists("non_existent_file_xyz_123.txt") == false);
    }
}

