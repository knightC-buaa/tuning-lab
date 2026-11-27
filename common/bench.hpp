#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace bench
{

struct Config
{
    int reps = 5;                 // 外层重复次数
    long inner_reps = 1;          // 每次重复内 body() 的调用次数
    bool inner_reps_set = false;  // 是否显式传入 --inner-reps
    int cpu = -1;                 // -1 不绑核；>=0 绑到该逻辑 CPU
    int warmup = 2;               // 把 cache、分支预测器预热, 跑 warmup 次 body()，不计入性能测量
    bool verify = false;          // 如果true, 只跑一次 body()，输出 checksum 后退出, 只做正确性校验，不做性能测量
};

struct Result
{
    std::string exp;
    std::string variant;
    int reps = 0;
    long inner_reps = 0;
    std::vector<double> samples_ns;  // 每次重复的「单次 body 耗时」
    double median_ns = 0;
    double min_ns = 0;
    double p90_ns = 0;
    double sys_ns = 0;  // 每次重复的平均系统态时间
    uint64_t checksum = 0;
    int cpu = -1;
    std::string governor;
    std::string ts;
    std::string semantics;  // 该实验有意保留的语义差异说明
    bool verify = false;
    std::string skip;       // 非空表示环境不满足
};

struct Skip : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

Config parse_args(int argc, char** argv);
double median(std::vector<double> xs);
double percentile(std::vector<double> xs, double p);
std::string to_json(const Result& r);

using Body = uint64_t (*)();
using Init = void (*)();

// 返回 0 成功，1 参数/校验错误，2 SKIP
int run(const std::string& exp_id, const std::string& variant, const Config& cfg, Body body, long default_inner_reps,
        Init init, const char* semantics);

}  // namespace bench

// VARIANT取值为{baseline optimized}二选一
#define BENCH_MAIN(EXP_ID, VARIANT, INNER_REPS)                                                                        \
    int main(int argc, char** argv)                                                                                    \
    {                                                                                                                  \
        auto cfg = bench::parse_args(argc, argv);                                                                      \
        return bench::run(EXP_ID, VARIANT, cfg, body, INNER_REPS, init, kSemantics);                                   \
    }
