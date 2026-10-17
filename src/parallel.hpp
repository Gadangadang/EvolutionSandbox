#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace evo {

// A small pool of worker threads for loops whose iterations are independent. The calling
// thread works too, and work is handed out in chunks, so fast and slow cores (Apple's
// performance and efficiency cores) both stay busy.
class ThreadPool {
public:
    explicit ThreadPool(int threads = 0);  // 0: one per hardware thread
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    int size() const { return static_cast<int>(workers_.size()) + 1; }

    // Runs body(begin, end) over [0, count) in chunks of `chunk`, then returns. Small loops run
    // on the calling thread alone, since waking the workers would cost more than it saves.
    void parallelFor(int count, int chunk, const std::function<void(int, int)>& body);

private:
    void workerLoop();
    void runChunks();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_, finished_;
    const std::function<void(int, int)>* body_ = nullptr;
    int count_ = 0, chunk_ = 1;
    std::atomic<int> next_{0};
    int busy_ = 0;
    uint64_t round_ = 0;
    bool stop_ = false;
};

// The pool shared by all worlds; its size can be set once, before first use.
ThreadPool& threadPool();
void setThreadCount(int threads);

}  // namespace evo
