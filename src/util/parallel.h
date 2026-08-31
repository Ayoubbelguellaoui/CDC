#ifndef OPENCDC_UTIL_PARALLEL_H
#define OPENCDC_UTIL_PARALLEL_H

#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <future>

namespace opencdc::util {

template<typename T, typename Func>
void parallel_for(std::vector<T>& items, Func func, size_t num_threads = 0) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    if (items.size() < num_threads) {
        num_threads = items.size();
    }
    
    if (num_threads <= 1) {
        for (auto& item : items) {
            func(item);
        }
        return;
    }
    
    std::atomic<size_t> index{0};
    std::atomic<bool> abort_flag{false};
    std::vector<std::thread> threads;
    std::exception_ptr first_exception;
    std::mutex exc_mutex;

    auto worker = [&]() {
        while (true) {
            if (abort_flag.load(std::memory_order_relaxed)) break;
            size_t i = index.fetch_add(1);
            if (i >= items.size()) break;
            try {
                func(items[i]);
            } catch (...) {
                std::lock_guard<std::mutex> lock(exc_mutex);
                if (!first_exception) first_exception = std::current_exception();
                abort_flag.store(true, std::memory_order_relaxed);
                break;
            }
        }
    };

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (first_exception) std::rethrow_exception(first_exception);
}

template<typename T, typename Func, typename Result>
std::vector<Result> parallel_map(const std::vector<T>& items, Func func, size_t num_threads = 0) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    std::vector<Result> results(items.size());
    
    if (items.size() < num_threads) {
        num_threads = items.size();
    }
    
    if (num_threads <= 1) {
        for (size_t i = 0; i < items.size(); ++i) {
            results[i] = func(items[i]);
        }
        return results;
    }
    
    std::atomic<size_t> index{0};
    std::atomic<bool> abort_flag{false};
    std::vector<std::thread> threads;
    std::exception_ptr first_exception;
    std::mutex exc_mutex;

    auto worker = [&]() {
        while (true) {
            if (abort_flag.load(std::memory_order_relaxed)) break;
            size_t i = index.fetch_add(1);
            if (i >= items.size()) break;
            try {
                results[i] = func(items[i]);
            } catch (...) {
                std::lock_guard<std::mutex> lock(exc_mutex);
                if (!first_exception) first_exception = std::current_exception();
                abort_flag.store(true, std::memory_order_relaxed);
                break;
            }
        }
    };

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (first_exception) std::rethrow_exception(first_exception);
    return results;
}

template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
        cond_.notify_one();
    }
    
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::condition_variable cond_;
};

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0) : running_(true) {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 4;
        }
        
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }
    
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cond_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    template<typename Func>
    auto submit(Func func) -> std::future<decltype(func())> {
        using Result = decltype(func());
        
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(func));
        std::future<Result> result = task->get_future();
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                throw std::runtime_error("submit() called on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        
        cond_.notify_one();
        return result;
    }
    
    size_t size() const { return workers_.size(); }
    
private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait(lock, [this]() { return !running_ || !tasks_.empty(); });
                
                if (!running_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            
            try {
                task();
            } catch (...) {
                // Exception in worker — task's future will receive the exception.
                // Do not crash the worker thread; continue processing remaining tasks.
            }
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
};

} // namespace opencdc::util

#endif // OPENCDC_UTIL_PARALLEL_H