#include "common/bench.hpp"
#include "common/black_box.hpp"
#include "test_util.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool approx(double a, double b) { return std::fabs(a - b) < 1e-9; }

std::string read_file(const char* path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

std::string capture_stdout(void (*child_main)(), int& exit_code) {
  std::string path =
      (std::filesystem::temp_directory_path() / "bench_test_XXXXXX").string();
  const int fd = mkstemp(path.data());
  CHECK(fd >= 0);

  const pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    CHECK(dup2(fd, STDOUT_FILENO) >= 0);
    close(fd);
    child_main();
    std::_Exit(99);
  }

  close(fd);
  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid);
  CHECK(WIFEXITED(status));
  exit_code = WEXITSTATUS(status);

  const std::string out = read_file(path.c_str());
  unlink(path.c_str());
  return out;
}

uint64_t body_42() { return 42; }
uint64_t body_7() { return 7; }
void init_noop() {}
void init_skip() { throw bench::Skip("no liburing"); }

void child_run_ok() {
  bench::Config cfg;
  cfg.reps = 2;
  cfg.warmup = 0;
  std::_Exit(bench::run("x", "baseline", cfg, body_42, 1, init_noop, "sem"));
}

void child_run_skip() {
  bench::Config cfg;
  cfg.reps = 1;
  cfg.warmup = 0;
  std::_Exit(bench::run("x", "optimized", cfg, body_42, 1, init_skip, ""));
}

void child_run_verify() {
  bench::Config cfg;
  cfg.verify = true;
  std::_Exit(bench::run("x", "baseline", cfg, body_7, 1, init_noop, ""));
}

void test_black_box() {
  volatile int v = 42;
  bench::keep(v);
  bench::clobber();
  CHECK(v == 42);
}

void test_median() {
  CHECK(approx(bench::median({1, 2, 3, 4, 5}), 3.0));
  CHECK(approx(bench::median({4, 1, 3, 2}), 2.5));
}

void test_percentile() {
  CHECK(approx(bench::percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0.9), 9.0));
}

void test_json() {
  bench::Result r;
  r.exp = "2.1.1_hash_vs_scan";
  r.variant = "baseline";
  r.reps = 5;
  r.inner = 1;
  r.median_ns = 123.0;
  r.min_ns = 100.0;
  r.p90_ns = 150.0;
  r.checksum = 0xabc;
  r.semantics = "same result";
  const std::string json = bench::to_json(r);
  CHECK(json.find("\"exp\"") != std::string::npos);
  CHECK(json.find("\"median_ns\"") != std::string::npos);
  CHECK(json.find("\"checksum\"") != std::string::npos);
  CHECK(json.find("\"semantics\"") != std::string::npos);
  CHECK(json.find("\"0x") != std::string::npos);

  bench::Result skipped;
  skipped.exp = "4.3.2_io_uring";
  skipped.variant = "optimized";
  skipped.skip = "缺少 liburing";
  const std::string skip_json = bench::to_json(skipped);
  CHECK(skip_json.find("\"skip\"") != std::string::npos);
}

void test_parse_args_valid() {
  char a0[] = "test_bench";
  char a1[] = "--reps";
  char a2[] = "3";
  char a3[] = "--inner";
  char a4[] = "2";
  char a5[] = "--cpu";
  char a6[] = "-1";
  char a7[] = "--warmup";
  char a8[] = "0";
  char a9[] = "--verify";
  char* argv[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, nullptr};
  const bench::Config cfg = bench::parse_args(10, argv);
  CHECK(cfg.reps == 3);
  CHECK(cfg.inner == 2);
  CHECK(cfg.inner_set);
  CHECK(cfg.cpu == -1);
  CHECK(cfg.warmup == 0);
  CHECK(cfg.verify);
}

void test_parse_args_rejects_zero_reps() {
  const pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    if (std::freopen("/dev/null", "w", stderr) == nullptr) {
      std::fprintf(stderr, "warning: 无法重定向 stderr\n");
    }
    char a0[] = "test_bench";
    char a1[] = "--reps";
    char a2[] = "0";
    char* argv[] = {a0, a1, a2, nullptr};
    bench::parse_args(3, argv);
    std::_Exit(0);
  }
  int status = 0;
  CHECK(waitpid(pid, &status, 0) == pid);
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 1);
}

void test_run_ok() {
  int code = -1;
  const std::string out = capture_stdout(child_run_ok, code);
  CHECK(code == 0);
  CHECK(out.find("\"checksum\":\"0x2a\"") != std::string::npos);
  CHECK(out.find("\"median_ns\"") != std::string::npos);
  CHECK(out.find("\"semantics\":\"sem\"") != std::string::npos);
}

void test_run_skip() {
  int code = -1;
  const std::string out = capture_stdout(child_run_skip, code);
  CHECK(code == 2);
  CHECK(out.find("\"skip\":\"no liburing\"") != std::string::npos);
}

void test_run_verify() {
  int code = -1;
  const std::string out = capture_stdout(child_run_verify, code);
  CHECK(code == 0);
  CHECK(out.find("\"verify\":true") != std::string::npos);
  CHECK(out.find("\"checksum\":\"0x7\"") != std::string::npos);
}

} // namespace

int main() {
  test_black_box();
  test_median();
  test_percentile();
  test_json();
  test_parse_args_valid();
  test_parse_args_rejects_zero_reps();
  test_run_ok();
  test_run_skip();
  test_run_verify();
  std::puts("test_bench: all checks passed");
  return 0;
}
