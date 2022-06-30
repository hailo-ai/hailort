/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
typedef pthread_t hailo_thread;
typedef void* thread_return_type;

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
    void *results = NULL;
    pthread_join(*thread, &results);
    return (hailo_status)results;
}

#elif defined _MSC_VER // __unix__ || __QNX__

#include <windows.h>
typedef HANDLE hailo_thread;
typedef DWORD thread_return_type;

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

#endif

#endif /* _HAILO_THREAD_H_ */
