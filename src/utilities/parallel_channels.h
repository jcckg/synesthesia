#pragma once

#include <algorithm>
#include "utilities/cancellation.h"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace Utilities {

class ParallelChannels {
public:
    class BatchScope {
    public:
        BatchScope() : previous_(batch_) { batch_ = true; }
        ~BatchScope() { batch_ = previous_; }
    private:
        bool previous_;
    };

    explicit ParallelChannels(std::size_t channels) {
        const auto desired = batch_ ? std::size_t{1} : channels;
        const auto lanes = std::min({desired, std::size_t{8},
            static_cast<std::size_t>(std::max(1u, std::thread::hardware_concurrency()))});
        try {
            workers_.reserve(lanes > 0 ? lanes - 1 : 0);
            for (std::size_t lane = 1; lane < lanes; ++lane)
                workers_.emplace_back([this] { work(); });
        } catch (const std::system_error&) {
            stop();
            workers_.clear();
            stopping_ = false;
        }
    }

    ~ParallelChannels() { stop(); }
    ParallelChannels(const ParallelChannels&) = delete;
    ParallelChannels& operator=(const ParallelChannels&) = delete;

    void run(std::size_t channels, const std::function<void(std::size_t)>& task) {
        if (workers_.empty()) {
            for (std::size_t channel = 0; channel < channels; ++channel) task(channel);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            task_ = &task;
            cancellation_ = operationCancellation;
            failed_ = false;
            channels_ = channels;
            next_.store(0, std::memory_order_relaxed);
            remaining_ = workers_.size();
            error_ = {};
            ++generation_;
        }
        ready_.notify_all();
        drain();
        std::unique_lock lock(mutex_);
        completed_.wait(lock, [this] { return remaining_ == 0; });
        task_ = nullptr;
        if (error_) std::rethrow_exception(error_);
    }

private:
    inline static thread_local bool batch_ = false;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable ready_, completed_;
    const std::function<void(std::size_t)>* task_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::size_t channels_ = 0, remaining_ = 0, generation_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    std::stop_token cancellation_;
    std::atomic<bool> failed_{false};

    void drain() {
        CancellationScope scope(cancellation_);
        for (;;) {
            if (failed_.load(std::memory_order_relaxed)) return;
            const auto channel = next_.fetch_add(1, std::memory_order_relaxed);
            if (channel >= channels_) return;
            try { (*task_)(channel); }
            catch (...) {
                std::lock_guard lock(mutex_);
                if (!error_) error_ = std::current_exception();
                failed_.store(true, std::memory_order_relaxed);
            }
        }
    }

    void work() {
        std::size_t generation = 0;
        std::unique_lock lock(mutex_);
        for (;;) {
            ready_.wait(lock, [&] { return stopping_ || generation_ != generation; });
            if (stopping_) return;
            generation = generation_;
            lock.unlock();
            drain();
            lock.lock();
            if (--remaining_ == 0) completed_.notify_one();
        }
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }
};

}
