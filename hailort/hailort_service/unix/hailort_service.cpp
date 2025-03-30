/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_service.cpp
 * @brief main for hailort service 
 * To run without daemonization run the hailort_service executable with `standalone`.
 *
 * To run as daemon service please follow the steps:
 *       1) Install the HailoRT:
 *           cmake -H. -Bbuild -DCMAKE_BUILD_TYPE=Release -DHAILO_BUILD_SERVICE=1 && sudo cmake --build build --target install
 *       
 *       2) Reload systemd manager configuration:
 *           sudo systemctl daemon-reload
 *       
 *       3) Enable and start the service 
 *           sudo systemctl enable --now hailort.service
 *
 *       4) Stop service
 *           sudo systemctl stop hailort.service
*/

#include "hailort_rpc_service.hpp"
#include "rpc/rpc_definitions.hpp"
#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "hailo/hailort_common.hpp"
#include "common/os_utils.hpp"

#include <syslog.h>
#include <sys/stat.h>

using namespace hailort;

bool is_default_service_address(const std::string server_address)
{
    return HAILORT_SERVICE_DEFAULT_ADDR == server_address;
}

bool socket_file_exists_and_unremovable()
{
    // Will return false in case we failed to remove the file for a reason other than "file doesn't exist"
    return ((unlink(HAILO_DEFAULT_SERVICE_ADDR.c_str()) != 0) && (errno != ENOENT));
}

void RunService()
{
    const std::string server_address = HAILORT_SERVICE_ADDRESS;

    // If the socket file already exists and cannot be removed due to insufficient permissions,
    // we should fail early to prevent grpc::BuildAndStart() from causing a segmentation fault.
    if (is_default_service_address(server_address) && socket_file_exists_and_unremovable()) {
        LOGGER__CRITICAL("Failed to remove existing socket file {}. This might indicate insufficient permissions for this operation.",
            HAILO_DEFAULT_SERVICE_ADDR);
        return;
    }

    HailoRtRpcService service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(-1);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    chmod(HAILO_DEFAULT_SERVICE_ADDR.c_str(), S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR);
    server->Wait();
}

void write_pid_to_lock_file()
{
    auto status = Filesystem::create_directory(HAILO_DAEMON_PID_DIR);
    if (status != HAILO_SUCCESS) {
        HAILORT_OS_LOG_ERROR("Cannot create directory at path, status={}", status);
        return;
    }

    auto locked_file = LockedFile::create(HAILO_DAEMON_PID_FILE, "wx");
    if (HAILO_SUCCESS != locked_file.status()) {
        HAILORT_OS_LOG_ERROR("Failed to lock pid file for hailort service, status={}", locked_file.status());
        return;
    }

    std::string pid = std::to_string(getpid());
    auto ret = write(locked_file->get_fd(), pid.c_str(), pid.size());
    if (-1 == ret) {
        HAILORT_OS_LOG_ERROR("Failed to write pid to lock file for hailort service, errno={}", errno);
        return;
    }
}

int main(int argc, char *argv[])
{
    bool is_standalone = ((1 < argc) && (strcmp("standalone", argv[1]) == 0));
    if (!is_standalone) {
        int ret = daemon(0, 0);
        if (ret < 0) {
            HAILORT_OS_LOG_ERROR("Failed to create daemon with errno {}", errno);
            exit(EXIT_FAILURE);
        }

        write_pid_to_lock_file();
    }
    RunService();
    return 0;
}