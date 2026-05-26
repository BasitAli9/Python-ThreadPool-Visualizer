#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(int n) : thread_status_(n) {
        for (int i = 0; i < n; ++i) {
            thread_status_[i].store(0);
            workers_.emplace_back([this, i]{ workerLoop(i); });
        }
    }

    ~ThreadPool() { shutdown(); for (auto& w : workers_) if (w.joinable()) w.join(); }

    void addTask(std::function<void()> task) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stop_.load()) throw std::runtime_error("pool stopped");
        ++unfinished_;
        tasks_.push(std::move(task));
        cv_.notify_one();
    }

    void join() {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this]{ return unfinished_.load() == 0; });
    }

    void shutdown() { stop_.store(true); cv_.notify_all(); }

    std::vector<int> getStatus() const {
        std::vector<int> s;
        for (auto& a : thread_status_) s.push_back(a.load());
        return s;
    }

    int size() const { return (int)workers_.size(); }

private:
    void workerLoop(int idx) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]{ return stop_.load() || !tasks_.empty(); });
                if (stop_.load() && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            thread_status_[idx].store(1);
            try { task(); } catch (...) {}
            thread_status_[idx].store(0);
            --unfinished_;
            cv_.notify_all();
        }
    }

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    std::atomic<bool>                 stop_{ false };
    std::atomic<int>                  unfinished_{ 0 };
    std::vector<std::atomic<int>>     thread_status_;
};
