#include "gtest/gtest.h"
#include "src/thread_pool/thread_pool.h"


namespace ctar
{
    TEST(ThreadPoolTest, ZeroThreads) { EXPECT_THROW(ctar::thread_pool pool(0), std::invalid_argument); }

    TEST(ThreadPoolTest, SubmitToStoppedPoolThrows)
    {
        ctar::thread_pool pool(2);
        pool.submit([] { return 1; });
        pool.stop();

        EXPECT_THROW(pool.submit([] { return 2; }), std::runtime_error);
    }


    TEST(ThreadPoolTest, SingleTask)
    {
        ctar::thread_pool pool(2);

        auto future = pool.submit([](int x) { return x * 2; }, 42);

        EXPECT_EQ(future.get(), 84);
    }


    TEST(ThreadPoolTest, MultipleTasks)
    {
        constexpr int kNumThreads = 4;
        constexpr int kNumTasks = 10000;

        ctar::thread_pool pool(kNumThreads);
        std::atomic<int> counter(0);
        std::vector<std::future<void>> futures;

        futures.reserve(kNumTasks);
        for (int i = 0; i < kNumTasks; ++i)
        {
            futures.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
        }

        for (auto& f : futures)
        {
            f.get();
        }

        EXPECT_EQ(counter.load(), kNumTasks);
    }

    TEST(ThreadPoolTest, TaskWithArguments)
    {
        ctar::thread_pool pool(2);

        auto future =
            pool.submit([](int a, int b, const std::string& s) { return std::to_string(a + b) + s; }, 10, 20, " Hello");

        EXPECT_EQ(future.get(), "30 Hello");
    }

    int sum(int a, int b) { return a + b; }

    TEST(ThreadPoolTest, TaskWithNamedFunc)
    {
        ctar::thread_pool pool(2);

        auto future = pool.submit(sum, 10, 20);

        EXPECT_EQ(future.get(), 30);
    }


    TEST(ThreadPoolTest, Size)
    {
        ctar::thread_pool pool(3);
        EXPECT_EQ(pool.size(), 3);
    }

    TEST(ThreadPoolTest, WaitCompletesAllTasks)
    {
        ctar::thread_pool pool(2);
        std::atomic<int> counter(0);

        for (int i = 0; i < 10; ++i)
        {
            pool.submit(
                [&counter]
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    counter.fetch_add(1);
                });
        }

        pool.wait();
        EXPECT_EQ(counter.load(), 10);
    }


    TEST(ThreadPoolTest, PendingTasks)
    {
        ctar::thread_pool pool(1);
        std::atomic<bool> is_first_task_done{false};

        auto f0 = pool.submit(
            [&is_first_task_done]
            {
                while (!is_first_task_done.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto f1 = pool.submit([] { return 1; });
        auto f2 = pool.submit([] { return 2; });

        EXPECT_EQ(pool.pending_tasks(), 2);

        is_first_task_done.store(true);

        f0.get();
        f1.get();
        f2.get();
    }
} // namespace ctar

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
