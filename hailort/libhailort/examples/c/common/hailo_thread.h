/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_thread.h
 * Common threads related functions, for linux and windows
 **/

#ifndef _HAILO_THREAD_H_
#define _HAILO_THREAD_H_

#include "hailo/hailort.h"

#if defined(__unix__) || defined(__QNX__)

#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>

typedef pthread_t hailo_thread;
typedef void* thread_return_type;
typedef atomic_int hailo_atomic_int;

#define MICROSECONDS_PER_MILLISECOND (1000)

hailo_status hailo_create_thread(thread_return_type(*func_ptr)(void*), void* args, hailo_thread *thread_out)
{
    int creation_results = pthread_create(thread_out, NULL, func_ptr, args);
    if (0 != creation_results) {
        return HAILO_INTERNAL_FAILURE;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_join_thread(hailo_thread *thread)
{
    uintptr_t join_results = 0;
    int err = pthread_join(*thread, (void*)&join_results);
    if (0 != err) {
        return HAILO_INTERNAL_FAILURE;
    }
    return (hailo_status)join_results;
}

void hailo_atomic_init(hailo_atomic_int *atomic, int value)
{
    atomic_init(atomic, value);
}

int hailo_atomic_load(hailo_atomic_int *atomic)
{
    return atomic_load(atomic);
}

int hailo_atomic_fetch_add(hailo_atomic_int *atomic, int value)
{
    return atomic_fetch_add(atomic, value);
}

void hailo_atomic_increment(hailo_atomic_int *atomic)
{
    atomic_fetch_add(atomic, 1);
}

void hailo_atomic_store(hailo_atomic_int *atomic, int value)
{
    atomic_store(atomic, value);
}

#elif defined _MSC_VER // __unix__ || __QNX__

#include <windows.h>
typedef HANDLE hailo_thread;
typedef DWORD thread_return_type;
typedef LONG hailo_atomic_int;

hailo_status hailo_create_thread(thread_return_type(func_ptr)(void*), void* args, hailo_thread *thread_out)
{
    *thread_out = CreateThread(NULL, 0, func_ptr, args, 0, NULL);
    if (NULL == *thread_out) {
        return HAILO_INTERNAL_FAILURE;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_join_thread(hailo_thread *thread)
{
    DWORD result;

    WaitForSingleObject(*thread, INFINITE);
    if (!GetExitCodeThread(*thread, &result)) {
        return HAILO_INTERNAL_FAILURE;
    }
    CloseHandle(*thread);
    return (hailo_status)result;
}

void hailo_atomic_init(hailo_atomic_int *atomic, int value)
{
    InterlockedExchange(atomic, (LONG)value);
}

int hailo_atomic_load(hailo_atomic_int *atomic)
{
    return InterlockedExchangeAdd(atomic, (LONG)0);
}

int hailo_atomic_fetch_add(hailo_atomic_int *atomic, int value)
{
    return InterlockedExchangeAdd(atomic, (LONG)value);
}

void hailo_atomic_increment(hailo_atomic_int *atomic)
{
    InterlockedIncrement(atomic);
}

void hailo_atomic_store(hailo_atomic_int *atomic, int value)
{
    InterlockedExchange(atomic, value);
}


#endif

#endif /* _HAILO_THREAD_H_ */
