#pragma once

#include_next <math.h>

#ifdef __cplusplus
#undef fpclassify
#undef isfinite
#undef isgreater
#undef isgreaterequal
#undef isinf
#undef isless
#undef islessequal
#undef islessgreater
#undef isnan
#undef isnormal
#undef isunordered
#undef signbit

namespace std {
inline long abs(long value) {
    return value < 0 ? -value : value;
}
inline long long abs(long long value) {
    return value < 0 ? -value : value;
}
inline float abs(float value) {
    return __builtin_fabsf(value);
}
inline double abs(double value) {
    return __builtin_fabs(value);
}
inline long double abs(long double value) {
    return __builtin_fabsl(value);
}
inline float fabs(float value) {
    return __builtin_fabsf(value);
}
inline double fabs(double value) {
    return __builtin_fabs(value);
}
inline long double fabs(long double value) {
    return __builtin_fabsl(value);
}
inline float sin(float value) {
    return __builtin_sinf(value);
}
inline double sin(double value) {
    return __builtin_sin(value);
}
inline long double sin(long double value) {
    return __builtin_sinl(value);
}
inline float cos(float value) {
    return __builtin_cosf(value);
}
inline double cos(double value) {
    return __builtin_cos(value);
}
inline long double cos(long double value) {
    return __builtin_cosl(value);
}
using ::isfinite;
using ::isinf;
using ::isnan;
using ::signbit;
} // namespace std
#endif
