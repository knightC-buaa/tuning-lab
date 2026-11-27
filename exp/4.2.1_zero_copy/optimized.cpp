#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <sys/sendfile.h>
#include <sys/types.h>
#include <unistd.h>

// 文件、连接与 sink 线程都在 init() 中建好，body() 只做 sendfile。
void init()
{
    init_base();
}

// sendfile 让内核把文件内容直接送进 socket，不经过用户态缓冲区。offset 显式
// 从 0 开始并由 sendfile 递增；每个 body() 都重新从文件头发送。
uint64_t body()
{
    std::uint64_t sent = 0;
    off_t offset = 0;
    const off_t total = static_cast<off_t>(kFileSize);
    while (offset < total) {
        const std::size_t want = std::min<std::size_t>(kChunkBytes, kFileSize - static_cast<std::size_t>(offset));
        const ssize_t n = ::sendfile(g_sink.client_fd(), g_file_fd, &offset, want);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n == 0) {
            throw std::runtime_error("sendfile 提前到达文件末尾");
        } else {
            fail_socket("sendfile", errno);
        }
    }

    bench::keep(sent);
    return sent;
}

BENCH_MAIN("4.2.1_zero_copy", "optimized", 4)
