#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

// 连接与服务线程都在 init() 中建好，body() 只做发送/接收。
void init()
{
    init_base();
}

// 先发出全部 64 个请求，再统一收齐 64 个响应，累加。
uint64_t body()
{
    for (int i = 0; i < kConnections; ++i) {
        const Connection& c = g_connections[i];
        send_full(c.client_fd, &c.request, kMessageBytes);
    }
    uint64_t sum = 0;
    for (int i = 0; i < kConnections; ++i) {
        uint64_t response = 0;
        recv_full(g_connections[i].client_fd, &response, kMessageBytes);
        sum += response;
    }
    bench::keep(sum);
    return sum;
}

BENCH_MAIN("4.1.1a_async_overlap", "optimized", 128)
