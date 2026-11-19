#pragma once
#include <atomic>

extern std::atomic<long long> triangleTests;

#ifdef M_PI
#undef M_PI
#endif

constexpr double M_PI = 3.14159265;