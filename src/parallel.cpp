#include "parallel.hpp"

#include <algorithm>

namespace evo {

ThreadPool::ThreadPool(int threads) {
    if (threads <= 0) threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    for (int i = 1; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : workers_) t.join();
}

void ThreadPool::runChunks() {
    for (int begin; (begin = next_.fetch_add(chunk_)) < count_;) (*body_)(begin, std::min(count_, begin + chunk_));
}

void ThreadPool::workerLoop() {
    uint64_t seen = 0;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || round_ != seen; });
            if (stop_) return;
            seen = round_;
        }
        runChunks();
        {
            std::lock_guard lock(mutex_);
            if (--busy_ == 0) finished_.notify_one();
        }
    }
}

void ThreadPool::parallelFor(int count, int chunk, const std::function<void(int, int)>& body) {
    chunk = std::max(1, chunk);
    if (workers_.empty() || count <= chunk) {
        if (count > 0) body(0, count);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        body_ = &body;
        count_ = count;
        chunk_ = chunk;
        next_ = 0;
        busy_ = static_cast<int>(workers_.size());
        ++round_;
    }
    wake_.notify_all();
    runChunks();
    std::unique_lock lock(mutex_);
    finished_.wait(lock, [&] { return busy_ == 0; });
}

namespace {
int requestedThreads = 0;
}

ThreadPool& threadPool() {
    static ThreadPool pool(requestedThreads);
    return pool;
}

void setThreadCount(int threads) { requestedThreads = threads; }

}  // namespace evo
