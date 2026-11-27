#pragma once

#include <vector>

namespace bench
{

// 每个物理核取一个逻辑 CPU，按序号升序。
std::vector<int> physical_cores();

// 成功返回 true。
bool pin_current_thread(int cpu);

// 当前线程所在的逻辑 CPU。
int current_cpu();

}  // namespace bench
