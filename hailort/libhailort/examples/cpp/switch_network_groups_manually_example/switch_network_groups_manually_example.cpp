/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file switch_network_groups_manually_example.cpp
 * This example demonstrates basic usage of HailoRT streaming api over multiple networks, using vstreams.
 * It loads several HEF networks with single/multiple inputs and single/multiple outputs into a Hailo VDevice and performs a
 * short inference on each one.
 * After inference is finished, the example switches to the next HEF and start inference again.
 **/

#include "hailo/hailort.hpp"

#include <iostream>
#include <chrono>
#include <thread>


constexpr hailo_format_type_t FORMAT_TYPE = HAILO_FORMAT_TYPE_AUTO;

constexpr size_t INFER_FRAME_COUNT = 100;
constexpr size_t RUN_COUNT = 10;
constexpr std::chrono::milliseconds WAIT_FOR_ACTIVATION_TIMEOUT_MS(10);
constexpr uint32_t DEVICE_COUNT = 1;

using namespace hailort;

#include <mutex>
#include <condition_variable>

class SyncObject final {
/* Synchronization class used to make sure I/O threads are blocking while their network_group is not activated  */
public:
    explicit SyncObject(size_t count) : m_original_count(count), m_count(count), m_all_arrived(false), m_mutex(), m_cv(), m_is_active(true)
        {};

    /* In main thread we wait until I/O threads are done (0 == m_count),
       signaling the I/O threads only after deactivating their network_group and resetting m_count to m_original_count */
    void wait_all(std::unique_ptr<ActivatedNetworkGroup> &&activated_network_group)
    {
        if (!m_is_active.load()) {
            return;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return ((0 == m_count) || !m_is_active); });
        activated_network_group.reset();
        m_count = m_original_count;
        m_all_arrived = true;
        m_cv.notify_all();
    }

    /* In I/O threads we wait until signaled by main thread (true == m_all_arrived),
       resetting m_all_arrived to false to make sure it was setted by 'wait_all' call */
    void notify_and_wait()
    {
        if (!m_is_active.load()) {
            return;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_all_arrived = false;
        --m_count;
        m_cv.notify_all();
        m_cv.wait(lock, [this] { return ((m_all_arrived) || !m_is_active); });
    }

    void terminate()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_is_active.store(false);
        }
        m_cv.notify_all();
    }

private:
    const size_t m_original_count;
    std::atomic_size_t m_count;
    std::atomic_bool m_all_arrived;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic_bool m_is_active;
};


void write_all(std::shared_ptr<ConfiguredNetworkGroup> network_group, InputVStream &input_vstream,
    std::shared_ptr<SyncObject> sync_object, std::shared_ptr<std::atomic_bool> should_threads_run, hailo_status &status_out)
{
    std::vector<uint8_t> buff(input_vstream.get_frame_size());

    auto status = HAILO_UNINITIALIZED;
    while (true) {
        if (!(*should_threads_run)) {
            break;
        }
        status = network_group->wait_for_activation(WAIT_FOR_ACTIVATION_TIMEOUT_MS);
        if (HAILO_TIMEOUT == status) {
            continue;
        } else if (HAILO_SUCCESS != status) {
            std::cerr << "Wait for network group activation failed. status = " << status << std::endl;
            status_out = status;
            return;
        }

        for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
            status = input_vstream.write(MemoryView(buff.data(), buff.size()));
            if (HAILO_SUCCESS != status) {
                status_out = status;
                return;
            }
        }
        sync_object->notify_and_wait();
    }
    return;
}

void read_all(std::shared_ptr<ConfiguredNetworkGroup> network_group, OutputVStream &output_vstream,
    std::shared_ptr<SyncObject> sync_object, std::shared_ptr<std::atomic_bool> should_threads_run, hailo_status &status_out)
{
    std::vector<uint8_t> buff(output_vstream.get_frame_size());

    auto status = HAILO_UNINITIALIZED;
    while (true) {
        if (!(*should_threads_run)) {
            break;
        }
        status = network_group->wait_for_activation(WAIT_FOR_ACTIVATION_TIMEOUT_MS);
        if (HAILO_TIMEOUT == status) {
            continue;
        } else if (HAILO_SUCCESS != status) {
            std::cerr << "Wait for network group activation failed. status = " << status << std::endl;
            status_out = status;
            return;
        }

        for (size_t i = 0; i < INFER_FRAME_COUNT; i++) {
            status = output_vstream.read(MemoryView(buff.data(), buff.size()));
            if (HAILO_SUCCESS != status) {
                status_out = status;
                return;
            }
        }
        sync_object->notify_and_wait();
    }
    return;
}

void network_group_thread_main(std::shared_ptr<ConfiguredNetworkGroup> network_group, std::shared_ptr<SyncObject> sync_object,
    std::shared_ptr<std::atomic_bool> should_threads_run, hailo_status &status_out)
{
    // Create VStreams
    auto vstreams_exp = VStreamsBuilder::create_vstreams(*network_group, {}, FORMAT_TYPE);
    if (!vstreams_exp) {
        std::cerr << "Failed to create vstreams, status = " << vstreams_exp.status() << std::endl;
        status_out = vstreams_exp.status();
        return;
    }

    // Create send/recv loops
    std::vector<std::unique_ptr<std::thread>> recv_ths;
    std::vector<hailo_status> read_results;
    read_results.reserve(vstreams_exp->second.size());
    for (auto &vstream : vstreams_exp->second) {
        read_results.push_back(HAILO_SUCCESS); // Success oriented
        recv_ths.emplace_back(std::make_unique<std::thread>(read_all,
            network_group, std::ref(vstream), sync_object, should_threads_run, std::ref(read_results.back())));
    }

    std::vector<std::unique_ptr<std::thread>> send_ths;
    std::vector<hailo_status> write_results;
    write_results.reserve(vstreams_exp->first.size());
    for (auto &vstream : vstreams_exp->first) {
        write_results.push_back(HAILO_SUCCESS); // Success oriented
        send_ths.emplace_back(std::make_unique<std::thread>(write_all,
            network_group, std::ref(vstream), std::ref(sync_object), should_threads_run, std::ref(write_results.back())));
    }

    for (auto &send_th : send_ths) {
        if (send_th->joinable()) {
            send_th->join();
        }
    }
    for (auto &recv_th : recv_ths) {
        if (recv_th->joinable()) {
            recv_th->join();
        }
    }

    for (auto &status : read_results) {
        if (HAILO_SUCCESS != status) {
            status_out = status;
            return;
        }
    }
    for (auto &status : write_results) {
        if (HAILO_SUCCESS != status) {
            status_out = status;
            return;
        }
    }
    status_out = HAILO_SUCCESS;
    return;
}

int main()
{
    hailo_vdevice_params_t params;
    auto status = hailo_init_vdevice_params(&params);
    if (HAILO_SUCCESS != status) {
        std::cerr << "Failed init vdevice_params, status = " << status << std::endl;
        return status;
    }

    params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;
    params.device_count = DEVICE_COUNT;
    auto vdevice_exp = VDevice::create(params);
    if (!vdevice_exp) {
        std::cerr << "Failed create vdevice, status = " << vdevice_exp.status() << std::endl;
        return vdevice_exp.status();
    }
    auto vdevice = vdevice_exp.release();

    std::vector<std::string> hef_paths = {"hefs/shortcut_net.hef", "hefs/shortcut_net.hef"};
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> configured_network_groups;

    for (const auto &path : hef_paths) {
        auto hef_exp = Hef::create(path);
        if (!hef_exp) {
            std::cerr << "Failed to create hef: " << path  << ", status = " << hef_exp.status() << std::endl;
            return hef_exp.status();
        }
        auto hef = hef_exp.release();

        auto added_network_groups = vdevice->configure(hef);
        if (!added_network_groups) {
            std::cerr << "Failed to configure vdevice, status = " << added_network_groups.status() << std::endl;
            return added_network_groups.status();
        }
        configured_network_groups.insert(configured_network_groups.end(), added_network_groups->begin(), added_network_groups->end());
    }

    auto should_threads_run = std::make_shared<std::atomic_bool>(true);

    std::vector<std::shared_ptr<SyncObject>> sync_objects;
    sync_objects.reserve(configured_network_groups.size());
    std::vector<hailo_status> threads_results;
    threads_results.reserve(configured_network_groups.size());
    std::vector<std::unique_ptr<std::thread>> network_group_threads;
    network_group_threads.reserve(configured_network_groups.size());

    for (auto network_group : configured_network_groups) {
        threads_results.push_back(HAILO_UNINITIALIZED);
        auto vstream_infos = network_group->get_all_vstream_infos();
        if (!vstream_infos) {
            std::cerr << "Failed to get vstream infos, status = " << vstream_infos.status() << std::endl;
            return vstream_infos.status();
        }
        sync_objects.emplace_back((std::make_shared<SyncObject>(vstream_infos->size())));
        network_group_threads.emplace_back(std::make_unique<std::thread>(network_group_thread_main,
            network_group, sync_objects.back(), should_threads_run, std::ref(threads_results.back())));
    }

    for (size_t i = 0; i < RUN_COUNT; i++) {
        for (size_t network_group_idx = 0; network_group_idx < configured_network_groups.size(); network_group_idx++) {
            auto activated_network_group_exp = configured_network_groups[network_group_idx]->activate();
            if (!activated_network_group_exp) {
                std::cerr << "Failed to activate network group, status = "  << activated_network_group_exp.status() << std::endl;
                return activated_network_group_exp.status();
            }
            sync_objects[network_group_idx]->wait_all(activated_network_group_exp.release());
        }
    }

    *should_threads_run = false;
    for (auto &sync_object : sync_objects) {
        sync_object->terminate();
    }

    for (auto &th : network_group_threads) {
        if (th->joinable()) {
            th->join();
        }
    }

    for (auto &thread_status : threads_results) {
        if (HAILO_SUCCESS != thread_status) {
            std::cerr << "Inference failed, status = "  << thread_status << std::endl;
            return thread_status;
        }
    }

    std::cout << "Inference finished successfully" << std::endl;
    return HAILO_SUCCESS;
}