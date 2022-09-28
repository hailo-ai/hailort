/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 *
 * @file hailort_service.cpp
 * @brief main for hailort service 
 * TODO: move to user guide (HRT-7559)
 *   * To run as without daemonize the executable:
 *       1) Compile with `./build.sh`
 *       2) Run `./bin/linux.x86_64.debug/hailort_service standalone`
 *
 *   * To run as daemon service please follow the steps:
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
#include "hailo/hailort_common.hpp"

#include <syslog.h>
#include <sys/stat.h>

void RunService() {
    std::string server_address(hailort::HAILO_DEFAULT_UDS_ADDR);
    hailort::HailoRtRpcService service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(-1);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    chmod(hailort::HAILO_DEFAULT_SERVICE_ADDR.c_str(), S_IROTH | S_IWOTH | S_IRUSR | S_IWUSR);
    server->Wait();
}

int main(int argc, char *argv[])
{
    bool is_standalone = ((1 < argc) && (strcmp("standalone", argv[1]) == 0));
    if (!is_standalone) {
        int ret = daemon(0,0);
        if (ret < 0) {
            syslog(LOG_ERR, "Failed to create daemon with errno %i", errno);
            exit(EXIT_FAILURE);
        }
    }

    RunService();
    return 0;
}