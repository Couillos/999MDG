#ifndef POWERMDG_DEBUG_LOG_H
#define POWERMDG_DEBUG_LOG_H

#include <cstdio>

inline bool g_verbose = false;

#define DEBUG_LOG(...) do { if (::g_verbose) std::fprintf(stderr, __VA_ARGS__); } while (false)

#endif
