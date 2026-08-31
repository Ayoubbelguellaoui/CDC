#include "util/parallel.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <atomic>

using namespace opencdc::util;

TEST(ThreadPoolTest, BasicSubmitAndResult) {
    ThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, MultipleTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1);
        }));
    }
    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), 20);
}

TEST(ThreadPoolTest, ConcurrentSubmitDuringShutdown) {
    std::atomic<int> completed{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 100; ++i) {
            pool.submit([&completed]() {
                completed.fetch_add(1);
            });
        }
    }
    EXPECT_EQ(completed.load(), 100);
}

TEST(ThreadPoolTest, WorkerSurvivesException) {
    ThreadPool pool(1);
    auto bad = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(bad.get(), std::runtime_error);
    auto good = pool.submit([]() { return 7; });
    EXPECT_EQ(good.get(), 7);
}

TEST(ThreadPoolTest, ZeroThreadsUsesHardwareConcurrency) {
    ThreadPool pool(0);
    EXPECT_GE(pool.size(), 1u);
}

TEST(ThreadSafeQueueTest, PushPopSize) {
    ThreadSafeQueue<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    q.push(10);
    q.push(20);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 2u);
    int val;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_FALSE(q.try_pop(val));
}

TEST(ParallelForTest, ProcessesAllItems) {
    std::vector<int> items = {1, 2, 3, 4, 5};
    std::atomic<int> sum{0};
    parallel_for(items, [&](int& item) {
        sum.fetch_add(item);
    });
    EXPECT_EQ(sum.load(), 15);
}

TEST(ParallelForTest, SingleThreadFallback) {
    std::vector<int> items = {10, 20};
    parallel_for(items, [](int& item) { item *= 2; }, 1);
    EXPECT_EQ(items[0], 20);
    EXPECT_EQ(items[1], 40);
}
