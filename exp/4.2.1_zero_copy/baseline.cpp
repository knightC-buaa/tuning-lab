#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <sys/types.h>
#include <unistd.h>

// 文件、连接与 sink 线程都在 init() 中建好，body() 只做 pread + write。
void init() { init_base(); }

// 64 KB 用户缓冲区：pread 读满一块，再循环 write 到 TCP socket。offset 显式
// 从 0 开始，每个 body() 都重新从文件头发送，不依赖文件当前偏移。
uint64_t body() {
  std::array<char, kChunkBytes> buf{};
  std::uint64_t sent = 0;
  off_t offset = 0;
  while (offset < static_cast<off_t>(kFileSize)) {
    const std::size_t want =
        std::min<std::size_t>(kChunkBytes,
                              kFileSize - static_cast<std::size_t>(offset));

    // 短读要循环到读满 want 字节。
    std::size_t got = 0;
    while (got < want) {
      const ssize_t n = ::pread(g_file_fd, buf.data() + got, want - got,
                                offset + static_cast<off_t>(got));
      if (n > 0) {
        got += static_cast<std::size_t>(n);
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n == 0) {
        throw std::runtime_error("pread 提前遇到文件末尾");
      } else {
        throw std::runtime_error(std::string("pread 失败: ") +
                                 std::strerror(errno));
      }
    }

    // 短写同样循环到写满 got 字节。
    std::size_t written = 0;
    while (written < got) {
      const ssize_t n =
          ::write(g_sink.client_fd(), buf.data() + written, got - written);
      if (n > 0) {
        written += static_cast<std::size_t>(n);
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n == 0) {
        throw std::runtime_error("write 返回 0，连接可能已断开");
      } else {
        fail_socket("write", errno);
      }
    }

    sent += written;
    offset += static_cast<off_t>(got);
  }

  bench::keep(sent);
  return sent;
}

BENCH_MAIN("4.2.1_zero_copy", "baseline", 2)
