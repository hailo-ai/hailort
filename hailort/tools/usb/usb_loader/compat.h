/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file compat.h
 * @brief Cross-platform compatibility layer for hailo_usb_loader (Linux/Windows)
 **/

#ifndef HAILO_USB_LOADER_COMPAT_H_
#define HAILO_USB_LOADER_COMPAT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libusb.h>

#define LOCK_BUSY_MESSAGE "Another hailo_usb_loader is running - please retry when it finishes"

#ifdef _WIN32

#include <windows.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Sleep wrappers */
#define usleep(us) Sleep((DWORD)(((us) + 999) / 1000))
#define sleep(s)   Sleep((DWORD)(s) * 1000)

/* Endianness - Windows on x86/x64/ARM64 is always little-endian */
#define le32toh(x) ((x))
#define htole32(x) ((x))

/* ssize_t for MSVC */
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

/* Lock abstraction - Windows uses Named Mutex */
typedef HANDLE lock_handle_t;
#define LOCK_INVALID NULL

#define LOCK_MUTEX_NAME "Global\\hailo_usb_loader" //TODO: make sure where to put these locks

#define RFS_SWU_DIR "C:\\Windows\\System32\\drivers\\hailo\\hailo10h_usb\\"

static inline uint64_t compat_monotonic_sec(void)
{
    return GetTickCount64() / 1000;
}

/**
 * Wait for a keypress or timeout.
 * Returns true if a key was pressed, false on timeout.
 */
static inline bool compat_wait_for_keypress_or_timeout(int timeout_sec)
{
    ULONGLONG start = GetTickCount64();
    ULONGLONG deadline = start + ((ULONGLONG)timeout_sec * 1000);

    while (GetTickCount64() < deadline) {
        if (_kbhit()) {
            _getch(); /* consume the key */
            return true;
        }
        Sleep(100);
    }
    return false;
}

static inline lock_handle_t lock_acquire(void)
{
    HANDLE mutex_handle = CreateMutexExA(NULL, LOCK_MUTEX_NAME, 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
    if (NULL == mutex_handle) {
        const DWORD error = GetLastError();
        // Exists only while a loader runs; access denied means a loader of another user or elevation level holds it
        if (ERROR_ACCESS_DENIED == error) {
            fprintf(stderr, "%s\n", LOCK_BUSY_MESSAGE);
        } else {
            fprintf(stderr, "Failed to create or open lock mutex: %lu\n", error);
        }
        return LOCK_INVALID;
    }
    DWORD wait = WaitForSingleObject(mutex_handle, 0);
    if (WAIT_TIMEOUT == wait) {
        fprintf(stderr, "%s\n", LOCK_BUSY_MESSAGE);
        CloseHandle(mutex_handle);
        return LOCK_INVALID;
    }
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        fprintf(stderr, "Failed to acquire lock: %lu\n", GetLastError());
        CloseHandle(mutex_handle);
        return LOCK_INVALID;
    }
    return mutex_handle;
}

static inline void lock_release(lock_handle_t lock)
{
    if (lock != LOCK_INVALID) {
        ReleaseMutex(lock);
        CloseHandle(lock);
    }
}

/**
 * Open a file for writing with platform-appropriate permissions.
 * On Windows, simply opens the file with fopen.
 * On Linux, uses open/fstat/fchmod/fdopen to set permissions.
 */
static inline FILE *compat_create_file_with_permissions(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (NULL == fp) {
        fprintf(stderr, "Failed to open file %s for writing\n", path);
    }
    return fp;
}

//TODO: change to reboot
#define HAILORTCLI_EXE "\"C:\\Program Files\\HailoRT\\bin\\hailortcli.exe\""
#define USB_DEVICE_ID_LEN 11 /* length of "usb/NNN:NNN" */

static inline bool compat_reset_handle(libusb_device_handle *handle)
{
    uint8_t type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE;
    int32_t ret = libusb_control_transfer(handle, type, REQ_SYS_REBOOT_FUNCTIONAL_MODE, 0, 0, NULL, 0, TIMEOUT_USB_MS);
    return (ret >= 0);
}

static inline void compat_disable_stdout_buffering(void)
{
    /* MSVC CRT defaults to full buffering on a redirected stdout; unflushed text is lost on crash. */
    setvbuf(stdout, NULL, _IONBF, 0);
}

#undef HAILORTCLI_EXE
#undef USB_DEVICE_ID_LEN

#else /* Linux */

#include <endian.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>

/* Lock abstraction - Linux uses file lock */
typedef int32_t lock_handle_t;
#define LOCK_INVALID (-1)

#define LOCK_FILE_DIR               "/run/lock/hailo/"
#define LOCK_FILE_DIR_PERMISSIONS   (0777)
#define LOCK_FILE_PATH              "/run/lock/hailo/hailo_usb.lock"
#define LOCK_FILE_PERMISSIONS       (0666)

#define RFS_SWU_DIR "/lib/firmware/hailo/hailo10h_usb/"

static inline uint64_t compat_monotonic_sec(void)
{
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec;
}

/**
 * Wait for a keypress or timeout.
 * Returns true if a key was pressed, false on timeout.
 */
static inline bool compat_wait_for_keypress_or_timeout(int timeout_sec)
{
    struct timeval timeout;
    fd_set readfds;

    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    return (0 < select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout));
}

static inline lock_handle_t lock_acquire(void)
{
    // Make sure LOCK_FILE_DIR exists before opening the lock file. If it does not exist,
    // create it with mode 0777 so any user can later create/edit the lock file inside it.
    // We must explicitly chmod() after mkdir() because the process umask will mask out
    // group/other write bits from the mkdir mode argument.
    struct stat dir_st;
    if (0 != stat(LOCK_FILE_DIR, &dir_st)) {
        if (0 != mkdir(LOCK_FILE_DIR, LOCK_FILE_DIR_PERMISSIONS)) {
            fprintf(stderr, "Failed to create lock directory %s: %s\n", LOCK_FILE_DIR, strerror(errno));
            return LOCK_INVALID;
        }
        if (0 != chmod(LOCK_FILE_DIR, LOCK_FILE_DIR_PERMISSIONS)) {
            fprintf(stderr, "Failed to set lock directory permissions on %s: %s\n", LOCK_FILE_DIR, strerror(errno));
            return LOCK_INVALID;
        }
    }

    int32_t fd = open(LOCK_FILE_PATH, O_CREAT | O_RDWR, LOCK_FILE_PERMISSIONS);
    if (fd < 0) {
        fprintf(stderr, "Failed to lock - please retry in a few seconds\n");
        return LOCK_INVALID;
    }

    struct stat st;
    if ((0 == fstat(fd, &st)) && (st.st_uid == getuid()) &&
        (0 != fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH))) {
        fprintf(stderr, "Failed to set lock file permissions to %o: %s\n", LOCK_FILE_PERMISSIONS, strerror(errno));
        close(fd);
        return LOCK_INVALID;
    }

    if (0 != flock(fd, LOCK_EX | LOCK_NB)) {
        if (EWOULDBLOCK == errno) {
            fprintf(stderr, "%s\n", LOCK_BUSY_MESSAGE);
        } else {
            fprintf(stderr, "Failed to acquire lock: %s\n", strerror(errno));
        }
        close(fd);
        return LOCK_INVALID;
    }

    return fd;
}

static inline void lock_release(lock_handle_t lock)
{
    if (lock >= 0) {
        flock(lock, LOCK_UN);
        close(lock);
    }
}

/**
 * Open a file for writing with platform-appropriate permissions.
 * On Linux, uses open/fstat/fchmod/fdopen to set permissions.
 */
static inline FILE *compat_create_file_with_permissions(const char *path)
{
    int32_t fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, LOCK_FILE_PERMISSIONS);
    if (fd < 0) {
        fprintf(stderr, "Failed to open file %s for writing: %s\n", path, strerror(errno));
        return NULL;
    }

    /* Only chmod if file owner */
    struct stat st;
    if ((0 == fstat(fd, &st)) && (st.st_uid == getuid())) {
        if (0 != fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) {
            fprintf(stderr, "Failed to set file permissions: %s\n", strerror(errno));
            close(fd);
            return NULL;
        }
    }

    FILE *fp = fdopen(fd, "w");
    if (NULL == fp) {
        fprintf(stderr, "Failed to fdopen file %s: %s\n", path, strerror(errno));
        close(fd);
        return NULL;
    }

    return fp;
}

static inline bool compat_reset_handle(libusb_device_handle *handle)
{
    int err = libusb_reset_device(handle);
    // libusb_reset_device returns LIBUSB_ERROR_NOT_FOUND when reset is successful but the device
    // changed its underlying state, or the device handle is no longer valid
    bool is_reset_ok = ((LIBUSB_SUCCESS == err) || (LIBUSB_ERROR_NOT_FOUND == err));
    if (!is_reset_ok) {
        fprintf(stderr, "Reset returned status %s\n", libusb_error_name(err));
    }
    return is_reset_ok;
}

static inline void compat_disable_stdout_buffering(void) { }

#endif /* _WIN32 */

#endif /* HAILO_USB_LOADER_COMPAT_H_ */
