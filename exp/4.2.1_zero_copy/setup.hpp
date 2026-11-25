#pragma once

// 实验 4.2.1：零拷贝。
//
// 256 MB 文件放在 $HOME/.cache/perf-opt-overview/zero_copy.bin（不能放 /tmp：
// 本机 /tmp 是 tmpfs，会把「磁盘 I/O vs 内存拷贝」的结论做反）。两变体把同一
// 份文件经 loopback TCP 发给同进程内的 sink 线程，sink 只读不存。
//
// baseline 用 64 KB 用户缓冲区 pread + write；optimized 用 sendfile 让内核
// 直接在文件与 socket 之间搬运。建文件、预热 page cache、建 TCP 连接、启动
// sink 线程都在 init() 中完成，body() 只负责发送。

#include "common/bench.hpp"
#include "common/cpu.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

constexpr std::size_t kFileSize = 256ull * 1024 * 1024;  // 256 MiB
constexpr std::size_t kChunkBytes = 64 * 1024;           // 64 KiB
// 建文件前要求约 300 MB 可用空间，给文件本身和文件系统元数据留余量。
constexpr std::uintmax_t kRequiredFreeBytes = 300ull * 1024 * 1024;
// sink 固定在逻辑 CPU 4；run_all.sh 把客户端主线程绑到 CPU 2，本机 2/3 与
// 4/5 分属不同物理核。手动运行时应避免 --cpu 4。
constexpr int kSinkCpu = 4;

inline std::filesystem::path data_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    throw bench::Skip("HOME 未设置，无法定位数据文件");
  }
  return std::filesystem::path(home) / ".cache" / "perf-opt-overview" /
         "zero_copy.bin";
}

// 创建 256 MB 文件：逐块写真实数据，不 ftruncate 成稀疏文件（稀疏文件读不
// 到磁盘 I/O，会让两变体都退化成纯内存拷贝）。先写临时文件再 rename，避免
// 其他进程看到写了一半的目标文件。
inline void create_data_file(const std::filesystem::path& path) {
  std::array<unsigned char, kChunkBytes> chunk{};
  for (std::size_t i = 0; i < kChunkBytes; ++i) {
    chunk[i] = static_cast<unsigned char>((i * 2654435761u) >> 24);
  }

  std::filesystem::path tmp = path;
  tmp += ".tmp." + std::to_string(static_cast<long>(::getpid()));
  const int fd =
      ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw bench::Skip(std::string("创建数据文件失败: ") + std::strerror(errno));
  }

  std::size_t written = 0;
  while (written < kFileSize) {
    const std::size_t want = std::min(kChunkBytes, kFileSize - written);
    std::size_t done = 0;
    while (done < want) {
      const ssize_t n = ::write(fd, chunk.data() + done, want - done);
      if (n > 0) {
        done += static_cast<std::size_t>(n);
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else {
        const int err = errno;
        ::close(fd);
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        throw bench::Skip(std::string("写入数据文件失败: ") +
                          std::strerror(err));
      }
    }
    written += want;
  }
  if (::close(fd) != 0) {
    const int err = errno;
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw bench::Skip(std::string("关闭数据文件失败: ") + std::strerror(err));
  }

  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw bench::Skip("重命名数据文件失败: " + ec.message());
  }
}

// 顺序读一遍文件预热 page cache；body() 的测量只应看到缓存命中后的拷贝/
// 搬运成本。
inline void warm_page_cache(int fd) {
  std::array<char, kChunkBytes> buf{};
  std::size_t done = 0;
  while (done < kFileSize) {
    const std::size_t want = std::min(kChunkBytes, kFileSize - done);
    const ssize_t n = ::pread(fd, buf.data(), want, static_cast<off_t>(done));
    if (n > 0) {
      done += static_cast<std::size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n == 0) {
      throw std::runtime_error("预热读取提前遇到文件末尾");
    } else {
      throw std::runtime_error(std::string("预热读取失败: ") +
                               std::strerror(errno));
    }
  }
}

// 确认/创建数据文件并以只读方式打开；文件只打开一次，供两变体共用。
inline int open_data_file() {
  const std::filesystem::path path = data_path();

  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    throw bench::Skip("创建数据目录失败: " + ec.message());
  }

  bool need_create = true;
  if (std::filesystem::is_regular_file(path, ec) && !ec) {
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size == kFileSize) {
      need_create = false;
    }
  }
  if (need_create) {
    const std::filesystem::space_info info =
        std::filesystem::space(path.parent_path(), ec);
    if (ec) {
      throw bench::Skip("无法读取磁盘可用空间: " + ec.message());
    }
    if (info.available < kRequiredFreeBytes) {
      throw bench::Skip("磁盘可用空间不足 300 MB");
    }
    create_data_file(path);
  }

  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    throw bench::Skip(std::string("打开数据文件失败: ") + std::strerror(errno));
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
      static_cast<std::uintmax_t>(st.st_size) != kFileSize) {
    ::close(fd);
    throw bench::Skip("数据文件大小不是 256 MB");
  }
  warm_page_cache(fd);
  return fd;
}

// loopback TCP sink：init() 里在 127.0.0.1:0 监听，主线程连上来，sink 线程
// accept 后循环 recv 丢弃。生命周期 RAII：析构时 shutdown 让阻塞的 recv 返回
// （或让 accept 退出）后 join；不要把可 join 的线程放进静态析构的全局容器。
class TcpSink {
 public:
  TcpSink() = default;
  ~TcpSink() { stop(); }

  TcpSink(const TcpSink&) = delete;
  TcpSink& operator=(const TcpSink&) = delete;

  // 建立监听与连接并启动 sink 线程；失败抛 bench::Skip。
  void start() {
    // 对端断开后发送端得到 EPIPE，而不是被 SIGPIPE 杀死。
    std::signal(SIGPIPE, SIG_IGN);

    const int lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) {
      throw bench::Skip(std::string("创建监听 socket 失败: ") +
                        std::strerror(errno));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(lfd, 1) != 0) {
      const int err = errno;
      ::close(lfd);
      throw bench::Skip(std::string("监听 socket 绑定/监听失败: ") +
                        std::strerror(err));
    }

    socklen_t addr_len = sizeof(addr);
    if (::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &addr_len) !=
        0) {
      const int err = errno;
      ::close(lfd);
      throw bench::Skip(std::string("读取监听端口失败: ") + std::strerror(err));
    }

    // backlog 让 connect 在 accept 之前就能完成，因此先连接再起线程不会阻塞。
    const int cfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (cfd < 0) {
      const int err = errno;
      ::close(lfd);
      throw bench::Skip(std::string("创建客户端 socket 失败: ") +
                        std::strerror(err));
    }
    if (::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      const int err = errno;
      ::close(cfd);
      ::close(lfd);
      throw bench::Skip(std::string("连接 loopback sink 失败: ") +
                        std::strerror(err));
    }

    // 诊断性兜底：若 sink 端异常退出导致无人消费，阻塞的 write/sendfile 在
    // 超时后返回 EAGAIN 并给出可诊断错误，而不是永久挂起。正常一次 256 MB
    // 传输约几十毫秒，30 秒留足余量。
    timeval send_timeout{};
    send_timeout.tv_sec = 30;
    ::setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
                 sizeof(send_timeout));

    listen_fd_ = lfd;
    client_fd_ = cfd;
    stop_.store(false, std::memory_order_relaxed);
    try {
      thread_ = std::thread([this] { serve(); });
    } catch (const std::system_error& e) {
      ::close(cfd);
      ::close(lfd);
      listen_fd_ = -1;
      client_fd_ = -1;
      throw bench::Skip(std::string("sink 线程创建失败: ") + e.what());
    }
  }

  int client_fd() const { return client_fd_; }
  int error() const { return error_.load(std::memory_order_relaxed); }

  void stop() noexcept {
    if (thread_.joinable()) {
      stop_.store(true, std::memory_order_release);
      // 本端 shutdown 会让对端（sink）的 recv 返回 0/EOF，从而退出循环；
      // 若线程尚未 accept，shutdown 监听 fd 也让 accept 立刻返回。
      if (client_fd_ >= 0) {
        ::shutdown(client_fd_, SHUT_RDWR);
      }
      if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
      }
      thread_.join();
    }
    close_fds();
  }

 private:
  void serve() {
    // 早于 bench::run 给主线程绑核，所以这里自行绑到核 4。
    bench::pin_current_thread(kSinkCpu);

    const int lfd = listen_fd_;
    int cfd = -1;
    while (true) {
      cfd = ::accept(lfd, nullptr, nullptr);
      if (cfd >= 0) {
        break;
      }
      if (errno == EINTR && !stop_.load(std::memory_order_acquire)) {
        continue;  // 信号打断后重试，避免 sink 线程提前退出
      }
      break;
    }
    if (cfd < 0) {
      if (!stop_.load(std::memory_order_acquire)) {
        error_.store(errno, std::memory_order_relaxed);
        // 让客户端感知连接已断，避免它在无人读取时灌满缓冲区后挂死。
        ::shutdown(client_fd_, SHUT_RDWR);
      }
      return;
    }

    std::array<char, kChunkBytes> buf{};
    while (!stop_.load(std::memory_order_acquire)) {
      const ssize_t n = ::recv(cfd, buf.data(), buf.size(), 0);
      if (n > 0) {
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n < 0 && !stop_.load(std::memory_order_acquire)) {
        error_.store(errno, std::memory_order_relaxed);
      }
      break;
    }
    ::close(cfd);
  }

  void close_fds() noexcept {
    if (client_fd_ >= 0) {
      ::close(client_fd_);
      client_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  std::atomic<bool> stop_{false};
  std::atomic<int> error_{0};
  int listen_fd_ = -1;
  int client_fd_ = -1;
  std::thread thread_;
};

inline TcpSink g_sink;
inline int g_file_fd = -1;

// 发送端遇到 socket 错误时统一抛出可诊断的 std::runtime_error。SIGPIPE 已被
// 忽略，对端断开表现为 EPIPE/ECONNRESET；若 sink 线程记录了具体错误则一并
// 带上，避免只看到发送端的次生错误。
[[noreturn]] inline void fail_socket(const char* what, int err) {
  std::string msg = std::string(what) + " 失败: " + std::strerror(err);
  const int sink_err = g_sink.error();
  if (sink_err != 0) {
    msg += "（sink 端错误: ";
    msg += std::strerror(sink_err);
    msg += "）";
  }
  throw std::runtime_error(msg);
}

// 建文件、预热缓存、建连接、启动 sink 线程。
inline void init_base() {
  if (g_file_fd >= 0) {
    return;  // bench::run 只调用一次 init()；保持幂等以便复用。
  }
  g_file_fd = open_data_file();
  g_sink.start();  // 失败抛 bench::Skip
}

constexpr const char* kSemantics =
    "两变体发送同一份 256 MB 文件到 loopback TCP sink，发送字节数相同（校验"
    "和一致）；baseline 经 64 KB 用户缓冲区做 pread + write，optimized 用 "
    "sendfile 由内核直接搬运；loopback 仍走内核协议栈，真实网卡上零拷贝省去"
    "用户态拷贝的收益更大。";
