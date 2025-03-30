/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file compiler_extensions_compat.hpp
 * @brief Defines compiler extensions and preprocessor macros that are compatible across MSVC and GNU compilers
 **/

#ifndef __COMPILER_EXTENSIONS_COMPAT_HPP__
#define __COMPILER_EXTENSIONS_COMPAT_HPP__


// https://stackoverflow.com/questions/1113409/attribute-constructor-equivalent-in-vc
// Initializer/finalizer sample for MSVC and GCC/Clang.
// 2010-2016 Joe Lowe. Released into the public domain.
#ifdef __cplusplus
    #define COMPAT__INITIALIZER(f) \
        static void f(void); \
        struct f##_t_ { f##_t_(void) { f(); } }; static f##_t_ f##_; \
        static void f(void)
#elif defined(_MSC_VER)
    #pragma section(".CRT$XCU",read)
    #define INITIALIZER2_(f,p) \
        static void f(void); \
        __declspec(allocate(".CRT$XCU")) void (*f##_)(void) = f; \
        __pragma(comment(linker,"/include:" p #f "_")) \
        static void f(void)
    #ifdef _WIN64
        #define COMPAT__INITIALIZER(f) INITIALIZER2_(f,"")
    #else
        #define COMPAT__INITIALIZER(f) INITIALIZER2_(f,"_")
    #endif
#else
    #define COMPAT__INITIALIZER(f) \
        static void f(void) __attribute__((constructor)); \
        static void f(void)
#endif

#if !defined(UNREFERENCED_PARAMETER) && !defined(_MSC_VER)
    #define UNREFERENCED_PARAMETER(param)   \
        do {                                \
            (void)(param);                  \
        } while(0)
#endif

// Used to capture consts in lambda expressions. On mscv constants must be captures, while in clang it
// causes a warning
#if _MSC_VER
#define LAMBDA_CONSTANT(constant_name) constant_name
#else
#define LAMBDA_CONSTANT(constant_name) constant_name=constant_name
#endif

#endif /* __COMPILER_EXTENSIONS_COMPAT_HPP__ */
