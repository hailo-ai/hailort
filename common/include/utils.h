/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.h
 * @brief Defines common utilities.
**/

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

/** A compile time assertion check.
 *
 *  Validate at compile time that the predicate is true without
 *  generating code. This can be used at any point in a source file
 *  where typedef is legal.
 *
 *  On success, compilation proceeds normally.
 *
 *  On failure, attempts to typedef an array type of negative size. The
 *  offending line will look like
 *      typedef assertion_failed_file_h_42[-1]
 *  where file is the content of the second parameter which should
 *  typically be related in some obvious way to the containing file
 *  name, 42 is the line number in the file on which the assertion
 *  appears, and -1 is the result of a calculation based on the
 *  predicate failing.
 *
 *  \param predicate The predicate to test. It must evaluate to
 *  something that can be coerced to a normal C boolean.
 *
 *  \param file A sequence of legal identifier characters that should
 *  uniquely identify the source file in which this condition appears.
 */
#define CASSERT(predicate, file) \
    _impl_CASSERT_LINE(predicate,__LINE__,file) \

#define _impl_PASTE(a,b) a##b
#define _impl_CASSERT_LINE(predicate, line, file) \
        ATTR_UNUSED typedef char _impl_PASTE(assertion_failed_##file##_,line)[2*!!(predicate)-1];

#define ARRAY_LENGTH(__array) (sizeof((__array)) / sizeof((__array)[0]))
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#endif

#ifndef DIV_ROUND_DOWN
#define DIV_ROUND_DOWN(n,d) ((n) / (d))
#endif

#ifndef ROUND_UNSIGNED_FLOAT
#define ROUND_UNSIGNED_FLOAT(n) ((n - (uint32_t)(n)) > 0.5) ? (uint32_t)(n + 1) : (uint32_t)(n)
#endif

#ifndef IS_POWEROF2
#define IS_POWEROF2(v) ((v & (v - 1)) == 0)
#endif

#define CPU_CYCLES_NUMBER_IN_MS (configCPU_CLOCK_HZ / 1000)

#define GET_MASK(width, shift) (((1U << (width)) - 1U) << (shift))

/* Argument counter for variadic macros */
#define GET_ARG_COUNT(...) INTERNAL_GET_ARG_COUNT_PRIVATE(0, ## __VA_ARGS__, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define INTERNAL_GET_ARG_COUNT_PRIVATE(_0, _1_, _2_, _3_, _4_, _5_, _6_, _7_, _8_, _9_, _10_, _11_, _12_, _13_, _14_, _15_, _16_, _17_, _18_, _19_, _20_, _21_, _22_, _23_, _24_, _25_, _26_, _27_, _28_, _29_, _30_, _31_, _32_, _33_, _34_, _35_, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, count, ...) count

#ifdef __GNUC__
  #define ATTR_UNUSED __attribute__((unused))
#else
  #define ATTR_UNUSED
#endif

#define PP_ARG_N(_1,  _2,  _3,  _4,  _5,  _6, N, ...) N

#define PP_RSEQ_N() 6,  5,  4,  3,  2,  1,  0

#define PP_NARG_(...)    PP_ARG_N(__VA_ARGS__)

#define PP_COMMASEQ_N()  1,  1,  1,  1,  1,  0

#define PP_COMMA(...)    ,

#define PP_NARG_HELPER3_01(N)    0
#define PP_NARG_HELPER3_00(N)    1
#define PP_NARG_HELPER3_11(N)    N
#define PP_NARG_HELPER2(a, b, N)    PP_NARG_HELPER3_ ## a ## b(N)
#define PP_NARG_HELPER1(a, b, N)    PP_NARG_HELPER2(a, b, N)

#define PP_HASCOMMA(...) \
        PP_NARG_(__VA_ARGS__, PP_COMMASEQ_N())

#define PP_NARG(...)                             \
        PP_NARG_HELPER1(                         \
            PP_HASCOMMA(__VA_ARGS__),            \
            PP_HASCOMMA(PP_COMMA __VA_ARGS__ ()),\
            PP_NARG_(__VA_ARGS__, PP_RSEQ_N()))

#define PP_ISEMPTY(...)                                 \
_PP_ISEMPTY(                                            \
          PP_HASCOMMA(__VA_ARGS__),                     \
          PP_HASCOMMA(PP_COMMA __VA_ARGS__),            \
          PP_HASCOMMA(__VA_ARGS__ (/*empty*/)),         \
          PP_HASCOMMA(PP_COMMA __VA_ARGS__ (/*empty*/)) \
          )
 
#define PP_PASTE5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define _PP_ISEMPTY(_0, _1, _2, _3) PP_HASCOMMA(PP_PASTE5(_PP_IS_EMPTY_CASE_, _0, _1, _2, _3))
#define _PP_IS_EMPTY_CASE_0001 ,

#define UNUSED0(...)
#define UNUSED1(x) (void)(x)
#define UNUSED2(x,y) (void)(x),(void)(y)
#define UNUSED3(x,y,z) (void)(x),(void)(y),(void)(z)
#define UNUSED4(a,x,y,z) (void)(a),(void)(x),(void)(y),(void)(z)
#define UNUSED5(a,b,x,y,z) (void)(a),(void)(b),(void)(x),(void)(y),(void)(z)
#define UNUSED6(a,b,x,y,z,c) (void)(a),(void)(b),(void)(x),(void)(y),(void)(z),(void)(c)

#define ALL_UNUSED_IMPL_(nargs) UNUSED ## nargs
#define ALL_UNUSED_IMPL(nargs) ALL_UNUSED_IMPL_(nargs)
#define ALL_UNUSED(...) ALL_UNUSED_IMPL( PP_NARG(__VA_ARGS__))(__VA_ARGS__ )

#define MICROSECONDS_IN_MILLISECOND (1000)

static inline uint8_t ceil_log2(uint32_t n)
{
    uint8_t result = 0;

    if (n <= 1) {
        return 0;
    }

    while (n > 1) {
        result++;
        n = (n + 1) >> 1;
    }

    return result;
}

#endif /* __UTILS_H__ */
