#include "common/bench.hpp"
#include "common/black_box.hpp"

#include "setup.hpp"

// 连接与服务线程都在 init() 中建好，body() 只做发送/接收。
void init()
{
    init_base();
}

// 64 个请求逐个发送并等待响应，累加 64 个响应值。
uint64_t body()
{
    uint64_t sum = 0;
    for (int i = 0; i < kConnections; ++i) {
        const Connection& c = g_connections[i];
        send_full(c.client_fd, &c.request, kMessageBytes);
        uint64_t response = 0;
        recv_full(c.client_fd, &response, kMessageBytes);
        sum += response;
    }
    bench::keep(sum);
    return sum;
}

BENCH_MAIN("4.1.1a_async_overlap", "baseline", 2)
