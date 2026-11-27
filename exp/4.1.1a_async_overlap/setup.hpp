#pragma once

// 实验 4.1.1a：异步等待重叠。
//
// 64 对 socketpair 连接到一个同进程内的单线程事件循环服务端。服务端收到
// 8 字节请求后记录 deadline = now + 1 ms，到期回 8 字节响应。
//
// baseline 逐个发送请求并等待响应；optimized 先发出全部 64 个请求，再统一
// 收齐响应。两者的请求值与「请求 -> 响应」映射完全相同，因此校验和一致。

#include "common/bench.hpp"
#include "common/cpu.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

constexpr int kConnections = 64;
constexpr std::size_t kMessageBytes = 8;
constexpr auto kServiceDelay = std::chrono::milliseconds(1);
// 服务线程固定在逻辑 CPU 4；run_all.sh 把客户端主线程绑到 CPU 2，
// 两者分属不同物理核（本机 2/3、4/5 各为一个物理核）。手动运行时应避免 --cpu 4。
constexpr int kServerCpu = 4;

struct Connection
{
    int client_fd = -1;
    int server_fd = -1;
    uint64_t request = 0;
};

inline std::array<Connection, kConnections> g_connections{};

// 请求值只由连接序号决定；响应值只由请求值决定，均与时间无关。
inline uint64_t request_value(int index)
{
    return static_cast<uint64_t>(index) * 0x9E3779B97F4A7C15ULL + 0x0123456789ABCDEFULL;
}

inline uint64_t response_value(uint64_t request)
{
    return (request ^ 0x94D049BB133111EBULL) * 0xBF58476D1CE4E5B9ULL;
}

// 在 SOCK_STREAM 上写满 len 字节；失败抛 std::runtime_error。
inline void send_full(int fd, const void* data, std::size_t len)
{
    const char* p = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            throw std::runtime_error(std::string("send 失败: ") + std::strerror(errno));
        }
    }
}

// 在 SOCK_STREAM 上读满 len 字节；失败或对端关闭抛 std::runtime_error。
inline void recv_full(int fd, void* data, std::size_t len)
{
    char* p = static_cast<char*>(data);
    std::size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n > 0) {
            got += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n == 0) {
            throw std::runtime_error("recv 失败: 对端已关闭");
        } else {
            throw std::runtime_error(std::string("recv 失败: ") + std::strerror(errno));
        }
    }
}

// 单线程事件循环服务端。生命周期 RAII：stop flag + eventfd 唤醒 ppoll + join。
// 不得把可 join 的 std::thread 放进静态析构的全局 vector。
class AsyncServer
{
public:
    AsyncServer() = default;
    ~AsyncServer()
    {
        stop();
    }

    AsyncServer(const AsyncServer&) = delete;
    AsyncServer& operator=(const AsyncServer&) = delete;

    void start()
    {
        if (thread_.joinable()) {
            return;
        }
        wake_fd_ = ::eventfd(0, EFD_CLOEXEC);
        if (wake_fd_ < 0) {
            throw bench::Skip(std::string("eventfd 创建失败: ") + std::strerror(errno));
        }
        stop_.store(false, std::memory_order_relaxed);
        try {
            thread_ = std::thread([this] { serve(); });
        } catch (const std::system_error& e) {
            ::close(wake_fd_);
            wake_fd_ = -1;
            throw bench::Skip(std::string("服务线程创建失败: ") + e.what());
        }
    }

    void stop() noexcept
    {
        if (thread_.joinable()) {
            stop_.store(true, std::memory_order_release);
            const uint64_t one = 1;
            ssize_t n = 0;
            do {
                n = ::write(wake_fd_, &one, sizeof(one));
            } while (n < 0 && errno == EINTR);
            thread_.join();
        }
        if (wake_fd_ >= 0) {
            ::close(wake_fd_);
            wake_fd_ = -1;
        }
    }

private:
    struct Pending
    {
        bool active = false;   // 请求已收齐，等待 deadline
        std::size_t have = 0;  // 已收到的请求字节数（用于短读）
        std::array<char, kMessageBytes> buffer{};
        std::chrono::steady_clock::time_point deadline{};
    };

    void serve()
    {
        bench::pin_current_thread(kServerCpu);

        std::array<pollfd, kConnections + 1> pfds{};
        pfds[0].fd = wake_fd_;
        pfds[0].events = POLLIN;
        for (int i = 0; i < kConnections; ++i) {
            pfds[i + 1].fd = g_connections[i].server_fd;
            pfds[i + 1].events = POLLIN;
        }
        std::array<Pending, kConnections> pending{};

        while (!stop_.load(std::memory_order_acquire)) {
            // 计算最近的 deadline，作为 ppoll 的超时。
            timespec timeout{};
            const timespec* timeout_ptr = nullptr;
            auto earliest = std::chrono::steady_clock::time_point::max();
            for (const Pending& p : pending) {
                if (p.active && p.deadline < earliest) {
                    earliest = p.deadline;
                }
            }
            if (earliest != std::chrono::steady_clock::time_point::max()) {
                const auto now = std::chrono::steady_clock::now();
                if (earliest <= now) {
                    timeout = timespec{0, 0};
                } else {
                    const auto remain = std::chrono::duration_cast<std::chrono::nanoseconds>(earliest - now);
                    timeout.tv_sec = static_cast<time_t>(remain.count() / 1'000'000'000);
                    timeout.tv_nsec = static_cast<long>(remain.count() % 1'000'000'000);
                }
                timeout_ptr = &timeout;
            }

            const int n = ::ppoll(pfds.data(), pfds.size(), timeout_ptr, nullptr);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;  // 无法恢复的 ppoll 错误：退出事件循环
            }
            if ((pfds[0].revents & POLLIN) != 0) {
                uint64_t drained = 0;
                ssize_t r = 0;
                do {
                    r = ::read(wake_fd_, &drained, sizeof(drained));
                } while (r < 0 && errno == EINTR);
                break;  // stop flag 已置位
            }

            // 读完整请求（SOCK_STREAM 可能短读，用 MSG_DONTWAIT 避免阻塞）。
            for (int i = 0; i < kConnections; ++i) {
                if ((pfds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                    continue;
                }
                Pending& p = pending[i];
                if (p.active) {
                    continue;  // 每连接同时只有一个在途请求
                }
                bool peer_gone = false;
                while (p.have < kMessageBytes) {
                    const ssize_t r = ::recv(g_connections[i].server_fd, p.buffer.data() + p.have,
                                             kMessageBytes - p.have, MSG_DONTWAIT);
                    if (r > 0) {
                        p.have += static_cast<std::size_t>(r);
                    } else if (r < 0 && errno == EINTR) {
                        continue;
                    } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;             // 剩余字节尚未到达，等下一次 POLLIN
                    } else {
                        peer_gone = true;  // EOF 或错误
                        break;
                    }
                }
                if (peer_gone) {
                    pfds[i + 1].fd = -1;  // ppoll 忽略负 fd
                    p = Pending{};
                    continue;
                }
                if (p.have == kMessageBytes) {
                    uint64_t request = 0;
                    std::memcpy(&request, p.buffer.data(), sizeof(request));
                    p.have = 0;
                    p.active = true;
                    p.deadline = std::chrono::steady_clock::now() + kServiceDelay;
                }
            }

            // 到期写回响应值。8 字节响应远小于 socket 缓冲，正常不会阻塞。
            const auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < kConnections; ++i) {
                Pending& p = pending[i];
                if (!p.active || p.deadline > now) {
                    continue;
                }
                uint64_t request = 0;
                std::memcpy(&request, p.buffer.data(), sizeof(request));
                const uint64_t response = response_value(request);
                const char* src = reinterpret_cast<const char*>(&response);
                std::size_t sent = 0;
                while (sent < kMessageBytes) {
                    const ssize_t w = ::write(g_connections[i].server_fd, src + sent, kMessageBytes - sent);
                    if (w > 0) {
                        sent += static_cast<std::size_t>(w);
                    } else if (w < 0 && errno == EINTR) {
                        continue;
                    } else {
                        break;  // 对端已关闭等；SIGPIPE 已被忽略
                    }
                }
                p.active = false;
            }
        }
    }

    std::atomic<bool> stop_{false};
    int wake_fd_ = -1;
    std::thread thread_;
};

inline AsyncServer g_server;

// 建立 64 对连接并启动服务线程；资源不满足时抛 bench::Skip。
inline void init_base()
{
    rlimit rl{};
    if (::getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        throw bench::Skip("无法读取 RLIMIT_NOFILE");
    }
    constexpr rlim_t kNeededFds = 256;  // 128 个 socketpair fd + 余量
    if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < kNeededFds) {
        throw bench::Skip("RLIMIT_NOFILE 不足 256");
    }

    // 服务端向已关闭连接写响应时不要杀掉进程。
    std::signal(SIGPIPE, SIG_IGN);

    int created = 0;
    for (int i = 0; i < kConnections; ++i) {
        int sv[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
            for (int j = 0; j < created; ++j) {
                ::close(g_connections[j].client_fd);
                ::close(g_connections[j].server_fd);
                g_connections[j] = Connection{};
            }
            throw bench::Skip(std::string("socketpair 创建失败: ") + std::strerror(errno));
        }
        g_connections[i].client_fd = sv[0];
        g_connections[i].server_fd = sv[1];
        g_connections[i].request = request_value(i);
        ++created;
    }

    g_server.start();  // 失败时抛 bench::Skip
}

constexpr const char* kSemantics = "服务端是同进程内的单线程事件循环，1 ms 是模拟处理延迟；两变体的请求/"
                                   "响应内容一致；本实验只测等待重叠，不测协程或线程管理成本。";
