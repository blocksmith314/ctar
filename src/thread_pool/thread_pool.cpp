#include "thread_pool.h"

namespace ctar
{
    thread_pool::thread_pool(size_t thread_cnt)
    {
        if (thread_cnt == 0)
        {
            throw std::invalid_argument("thread_pool requires at least 1 thread");
        }
        worker_thread_cnt_ = thread_cnt;

        workers_.reserve(worker_thread_cnt_);
        for (size_t i = 0; i < worker_thread_cnt_; ++i)
        {
            workers_.emplace_back(&thread_pool::worker, this);
        }
    }

    void thread_pool::stop()
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (stop_)
        {
            return;
        }
        stop_ = true;
        cv_task_available_.notify_all();
    }

    void thread_pool::join()
    {
        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers_.clear();
    }

    thread_pool::~thread_pool()
    {
        stop();
        join();
    }

    void thread_pool::worker()
    {
        while (true)
        {
            std::function<void()> task;

            
                    {
                        

                        std::unique_lock<std::mutex> lock(worker_mutex_);

                        cv_task_available_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                        if (stop_)
                        {
                            // Do not start tasks that are still waiting in the queue.
                            cv_task_done_.notify_all();
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                        active_tasks_++;
                    }

            task();

            
                    {
                        

                        std::lock_guard<std::mutex> lock(worker_mutex_);
                        active_tasks_--;
                        if (active_tasks_ == 0 && tasks_.empty())
                        {
                            cv_task_done_.notify_all();
                        }
                    }
        }
    }

    void thread_pool::wait()
    {
        std::unique_lock<std::mutex> lock(worker_mutex_);
        cv_task_done_.wait(lock, [this] { return active_tasks_ == 0 && (tasks_.empty() || stop_); });
    }

    size_t thread_pool::pending_tasks() const
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        return tasks_.size();
    }
} // namespace ctar