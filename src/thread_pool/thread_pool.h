#ifndef CTAR_THREAD_POOL_H
#define CTAR_THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace ctar
{
    class thread_pool
    {
    public:
        explicit thread_pool(size_t thread_cnt = std::thread::hardware_concurrency());

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

        thread_pool(thread_pool&&) = delete;
        thread_pool& operator=(thread_pool&&) = delete;

        ~thread_pool();

        template <typename F, typename... Args>
        auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

        size_t size() const { return workers_.size(); }

        size_t pending_tasks() const;


        // Wait for all currently running tasks to finish.
        // Pending tasks are not executed after stop() is called.
        void wait();


        // Stop workers. Running tasks finish, while pending tasks are discarded.
        void stop();
        void join();

    private:
        void worker();

        size_t worker_thread_cnt_ = 0;
        size_t active_tasks_ = 0;
        bool stop_ = false;

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        mutable std::mutex worker_mutex_;
        std::condition_variable cv_task_available_;
        std::condition_variable cv_task_done_;
    };

    template <typename F, typename... Args>
    auto thread_pool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable
            { return std::apply(std::forward<decltype(f)>(f), std::forward<decltype(args)>(args)); });

        std::future res = task->get_future();

        {
            std::lock_guard<std::mutex> lock(worker_mutex_);
            if (stop_)
            {
                throw std::runtime_error("Cannot submit task to stopped thread pool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }

        cv_task_available_.notify_one();

        return res;
    }
} // namespace ctar

#endif // CTAR_THREAD_POOL_H
