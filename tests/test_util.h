// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define DENOISE_CHECK(cond)                                                          \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "CHECK FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            std::exit(1);                                                            \
        }                                                                            \
    } while (0)

#define DENOISE_CHECK_NEAR(a, b, eps)                                                            \
    do {                                                                                         \
        const double _a = (a), _b = (b);                                                         \
        if (std::fabs(_a - _b) > (eps)) {                                                        \
            std::fprintf(stderr, "CHECK_NEAR FAILED: %s (%.9g) vs %s (%.9g) at %s:%d\n", #a, _a,  \
                          #b, _b, __FILE__, __LINE__);                                            \
            std::exit(1);                                                                        \
        }                                                                                        \
    } while (0)
