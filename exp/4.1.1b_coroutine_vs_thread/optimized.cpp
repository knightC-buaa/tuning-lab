#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

#include <coroutine>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <time.h>

namespace
{

// 单线程事件循环：所有任务共享同一个 deadline，因此 timer 容器只需一个
// handle 向量，不必维护最小堆。
struct EventLoop
{
    std::vector<std::coroutine_handle<>> pending;

    void suspend(std::coroutine_handle<> handle)
    {
        pending.push_back(handle);
    }

    void run_until(std::chrono::steady_clock::time_point deadline)
    {
        // libstdc++ 的 steady_clock 基于 CLOCK_MONOTONIC，可直接换算成绝对时间。
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
        timespec ts{};
        ts.tv_sec = static_cast<time_t>(ns / 1'000'000'000);
        ts.tv_nsec = static_cast<long>(ns % 1'000'000'000);

        int rc = 0;
        do {
            rc = ::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        } while (rc == EINTR);
        if (rc != 0) {
            throw std::runtime_error(std::string("clock_nanosleep 失败: ") + std::strerror(rc));
        }

        // 全部任务在同一 deadline 到期：逐个 resume 到完成，再由事件循环统一
        // destroy frame（Task 包装器不负责销毁，避免双重销毁）。
        for (const std::coroutine_handle<> handle : pending) {
            handle.resume();
            if (handle.done()) {
                handle.destroy();
            }
        }
        pending.clear();
    }
};

struct TimerAwaiter
{
    EventLoop& loop;

    bool await_ready() const noexcept
    {
        return false;
    }
    void await_suspend(std::coroutine_handle<> handle) const
    {
        loop.suspend(handle);
    }
    void await_resume() const noexcept
    {
    }
};

struct Task
{
    struct promise_type
    {
        Task get_return_object() noexcept
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }
        std::suspend_always final_suspend() noexcept
        {
            return {};
        }
        void return_void() noexcept
        {
        }
        void unhandled_exception() noexcept
        {
            std::terminate();
        }
    };

    std::coroutine_handle<promise_type> handle;
    // 不提供析构函数：frame 归 EventLoop 所有，done() 后由它 destroy。
};

Task task_body(int index, EventLoop& loop)
{
    co_await TimerAwaiter{loop};
    g_out[static_cast<std::size_t>(index)] = task_value(index);
}

}  // namespace

void init()
{
    init_base();
}

// 每个任务一个协程：创建、单次等待、resume 与 frame 释放都在这里完成。
uint64_t body()
{
    const auto deadline = std::chrono::steady_clock::now() + kTaskDelay;

    EventLoop loop;
    loop.pending.reserve(static_cast<std::size_t>(kTasks));
    for (int i = 0; i < kTasks; ++i) {
        // suspend_never 起步：调用即执行到 co_await 并把 handle 交给事件循环。
        static_cast<void>(task_body(i, loop));
    }
    loop.run_until(deadline);

    const uint64_t sum = sum_out();
    bench::keep(sum);
    return sum;
}

BENCH_MAIN("4.1.1b_coroutine_vs_thread", "optimized", 128)
