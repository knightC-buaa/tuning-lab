#pragma once

namespace bench
{

template <class T>
inline void keep(const T& v)
{
    asm volatile("" : : "r,m"(v) : "memory");
}

inline void clobber()
{
    asm volatile("" ::: "memory");
}

}  // namespace bench
