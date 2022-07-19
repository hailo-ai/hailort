/*
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include "network_group_handle.hpp"

#include <sstream>
#include <chrono>

VDeviceManager NetworkGroupHandle::m_vdevice_manager;
NetworkGroupConfigManager NetworkGroupHandle::m_net_group_config_manager;
NetworkGroupActivationManager NetworkGroupHandle::m_net_group_activation_manager;

Expected<std::shared_ptr<VDevice>> NetworkGroupHandle::create_vdevice(const std::string &device_id, uint16_t device_count, uint32_t vdevice_key,
    hailo_scheduling_algorithm_t scheduling_algorithm)
{
    auto expected_device = m_vdevice_manager.create_vdevice(m_element, device_id, device_count, vdevice_key, scheduling_algorithm);
    GST_CHECK_EXPECTED(expected_device, m_element, RESOURCE, "Failed creating vdevice, status = %d", expected_device.status());
    return expected_device;
}

hailo_status NetworkGroupHandle::set_hef(const char *device_id, uint16_t device_count, uint32_t vdevice_key,
    hailo_scheduling_algorithm_t scheduling_algorithm, const char *hef_path)
{
    if (0 == device_count) {
        device_count = HAILO_DEFAULT_DEVICE_COUNT;
    }

    std::string device_id_str = (nullptr == device_id) ? "" : device_id;

    auto vdevice = create_vdevice(device_id_str, device_count, vdevice_key, scheduling_algorithm);
    GST_CHECK_EXPECTED_AS_STATUS(vdevice, m_element, RESOURCE, "Failed creating vdevice, status = %d", vdevice.status());
    m_vdevice = vdevice.release();

    // Setting m_shared_device_id only if a non-default vdevice_key or explicit device_id is given
    if (!device_id_str.empty()) {
        m_shared_device_id = device_id;
    } else if (DEFAULT_VDEVICE_KEY != vdevice_key) {
        m_shared_device_id = std::to_string(device_count) + "-" + std::to_string(vdevice_key);
    } else {
        m_shared_device_id = "";
    }

    auto hef = Hef::create(hef_path);
    GST_CHECK_EXPECTED_AS_STATUS(hef, m_element, RESOURCE, "Failed reading hef file %s, status = %d", hef_path, hef.status());

    m_hef = make_shared_nothrow<Hef>(std::move(hef.release()));
    GST_CHECK(nullptr != m_hef, HAILO_OUT_OF_HOST_MEMORY, m_element, RESOURCE, "Allocating memory for HEF has failed!");

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupHandle::configure_network_group(const char *net_group_name, uint16_t batch_size)
{
    auto net_groups_params_map = get_configure_params(*m_hef, net_group_name, batch_size);
    GST_CHECK_EXPECTED_AS_STATUS(net_groups_params_map, m_element, RESOURCE, "Failed getting configure params, status = %d", net_groups_params_map.status());

    auto expected_cng = m_net_group_config_manager.configure_network_group(m_element, m_shared_device_id, net_group_name, batch_size, m_vdevice, m_hef, net_groups_params_map.value());
    GST_CHECK_EXPECTED_AS_STATUS(expected_cng, m_element, RESOURCE, "Failed configuring network, status = %d", expected_cng.status());

    m_cng = expected_cng.release();
    m_net_group_name = net_group_name;
    m_batch_size = batch_size;
    return HAILO_SUCCESS;
}


hailo_status NetworkGroupHandle::set_scheduler_timeout(const char *network_name, uint32_t timeout_ms)
{
    return m_cng->set_scheduler_timeout(std::chrono::milliseconds(timeout_ms), network_name);
}

hailo_status NetworkGroupHandle::set_scheduler_threshold(const char *network_name, uint32_t threshold)
{
    return m_cng->set_scheduler_threshold(threshold, network_name);
}

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> NetworkGroupHandle::create_vstreams(const char *network_name,
    const std::vector<hailo_format_with_name_t> &output_formats)
{
    GST_CHECK(nullptr != network_name, make_unexpected(HAILO_INVALID_ARGUMENT), m_element, RESOURCE, "Got nullptr in network name!");

    m_network_name = network_name;
    hailo_status status = m_net_group_config_manager.add_network_to_shared_network_group(m_shared_device_id, m_network_name, m_element);
    GST_CHECK(HAILO_SUCCESS == status, make_unexpected(status), m_element, RESOURCE,
        "Inserting network name to configured networks has failed, status = %d", status);

    auto input_params_map = m_cng->make_input_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, m_network_name);
    GST_CHECK_EXPECTED(input_params_map, m_element, RESOURCE, "Failed making input vstream params, status = %d",
        input_params_map.status());

    auto input_vstreams = VStreamsBuilder::create_input_vstreams(*m_cng, input_params_map.release());
    GST_CHECK_EXPECTED(input_vstreams, m_element, RESOURCE, "Failed creating input vstreams, status = %d", input_vstreams.status());

    // TODO: HRT-4095
    GST_CHECK(1 == input_vstreams->size(), make_unexpected(HAILO_INVALID_OPERATION), m_element, RESOURCE,
        "hailosend element supports only HEFs with one input for now!");

    auto output_params_map = m_cng->make_output_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, m_network_name);
    GST_CHECK_EXPECTED(output_params_map, m_element, RESOURCE, "Failed making output vstream params, status = %d",
        output_params_map.status());
    
    if (output_formats.size() > 0) {
        std::unordered_map<std::string, hailo_format_t> output_formats_map;
        for (const auto &format_with_name : output_formats) {
            GST_CHECK(output_formats_map.find(format_with_name.name) == output_formats_map.end(), make_unexpected(HAILO_INVALID_ARGUMENT), m_element, RESOURCE,
                "Got duplicate output format from event! (name = %s)", format_with_name.name);
            GST_CHECK(output_params_map->find(format_with_name.name) != output_params_map->end(), make_unexpected(HAILO_INVALID_ARGUMENT),
                m_element, RESOURCE, "Got unknown output format from event! (name = %s)", format_with_name.name);
            output_formats_map[format_with_name.name] = format_with_name.format;
        }
        for (auto &vstream_params : output_params_map.value()) {
            vstream_params.second.user_buffer_format = output_formats_map[vstream_params.first];
        }
    }

    auto output_vstreams = VStreamsBuilder::create_output_vstreams(*m_cng, output_params_map.release());
    GST_CHECK_EXPECTED(output_vstreams, m_element, RESOURCE, "Failed creating output vstreams, status = %d", output_vstreams.status());

    return std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>(
        std::move(input_vstreams.release()), std::move(output_vstreams.release()));
}

Expected<NetworkGroupsParamsMap> NetworkGroupHandle::get_configure_params(Hef &hef, const char *net_group_name, uint16_t batch_size)
{
    auto params = hef.create_configure_params(HAILO_STREAM_INTERFACE_PCIE, net_group_name);
    GST_CHECK_EXPECTED(params, m_element, RESOURCE, "Failed creating configure params, status = %d", params.status());
    params->batch_size = batch_size;

    NetworkGroupsParamsMap net_groups_params_map;
    net_groups_params_map[net_group_name] = std::move(params.release());
    return net_groups_params_map;
}

hailo_status NetworkGroupHandle::activate_network_group()
{
    auto expected_ang = m_net_group_activation_manager.activate_network_group(m_element, m_shared_device_id, m_net_group_name.c_str(), m_batch_size, m_cng);
    GST_CHECK_EXPECTED_AS_STATUS(expected_ang, m_element, RESOURCE, "Failed activating network, status = %d", expected_ang.status());
    m_ang = expected_ang.release();
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupHandle::abort_streams()
{
    if (nullptr == m_cng) {
        return HAILO_SUCCESS;
    }

    hailo_status final_status = HAILO_SUCCESS;
    auto input_streams = m_cng->get_input_streams_by_network(m_network_name);
    GST_CHECK_EXPECTED_AS_STATUS(input_streams, m_element, RESOURCE, "Getting input streams by network name %s failed, status = %d",
        m_network_name.c_str(), input_streams.status());

    for (auto &input_stream : input_streams.value()) {
        hailo_status status = input_stream.get().abort();
        if (HAILO_SUCCESS != status) {
            g_warning("Abort of input stream %s has failed, status = %d", input_stream.get().name().c_str(), status);
            final_status = status;
        }
    }

    auto output_streams = m_cng->get_output_streams_by_network(m_network_name);
    GST_CHECK_EXPECTED_AS_STATUS(output_streams, m_element, RESOURCE, "Getting output streams by network name %s failed, status = %d",
        m_network_name.c_str(), output_streams.status());

    for (auto &output_stream : output_streams.value()) {
        hailo_status status = output_stream.get().abort();
        if (HAILO_SUCCESS != status) {
            g_warning("Abort of output stream %s has failed, status = %d", output_stream.get().name().c_str(), status);
            final_status = status;
        }
    }
    return final_status;
}

Expected<bool> NetworkGroupHandle::remove_network_group()
{
    bool was_network_deactivated = false;

    // If use count is 2, it means the only references to the activated network group is in the manager and the one here, meaning that we can clear it
    // from the manager
    if (m_ang.use_count() == 2) {
        hailo_status status = m_net_group_activation_manager.remove_activated_network(m_shared_device_id, m_net_group_name.c_str(), m_batch_size);
        GST_CHECK(HAILO_SUCCESS == status, make_unexpected(status), m_element, RESOURCE, "Cound not find activated network group! status = %d", status);

        was_network_deactivated = true;
    }

    // Delete local activated network group
    m_ang.reset();

    return was_network_deactivated;
}


Expected<std::shared_ptr<VDevice>> VDeviceManager::create_vdevice(const void *element, const std::string &device_id, uint16_t device_count,
    uint32_t vdevice_key, hailo_scheduling_algorithm_t scheduling_algorithm)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!device_id.empty()) {
        return create_shared_vdevice(element, device_id, scheduling_algorithm);
    }
    if (DEFAULT_VDEVICE_KEY != vdevice_key) {
        return create_shared_vdevice(element, device_count, vdevice_key, scheduling_algorithm);
    }
    return create_unique_vdevice(element, device_count, scheduling_algorithm);
}

Expected<std::shared_ptr<VDevice>> VDeviceManager::create_shared_vdevice(const void *element, const std::string &device_id,
    hailo_scheduling_algorithm_t scheduling_algorithm)
{
    // If passing device_id, than device_count must be 1
    const auto device_count = 1;

    // If vdevice already exist, use it
    auto found_vdevice = get_vdevice(device_id, scheduling_algorithm);
    if (found_vdevice.status() != HAILO_NOT_FOUND) {
        GST_CHECK_EXPECTED(found_vdevice, element, RESOURCE, "Failed using shared vdevice, status = %d", found_vdevice.status());
        return found_vdevice.release();
    }

    auto device_info_expected = Device::parse_pcie_device_info(device_id);
    GST_CHECK_EXPECTED(device_info_expected, element, RESOURCE, "Failed parsing pcie device info, status = %d", device_info_expected.status());

    hailo_vdevice_params_t params = {};
    params.device_count = device_count;
    params.device_infos = &(device_info_expected.value());
    params.scheduling_algorithm = scheduling_algorithm;
    auto vdevice = VDevice::create(params);
    GST_CHECK_EXPECTED(vdevice, element, RESOURCE, "Failed creating vdevice, status = %d", vdevice.status());
    std::shared_ptr<VDevice> vdevice_ptr = std::move(vdevice.release());

    m_shared_vdevices[device_id] = vdevice_ptr;
    m_shared_vdevices_scheduling_algorithm[device_id] = scheduling_algorithm;
    return vdevice_ptr;
}

Expected<std::shared_ptr<VDevice>> VDeviceManager::create_shared_vdevice(const void *element, uint16_t device_count, uint32_t vdevice_key,
    hailo_scheduling_algorithm_t scheduling_algorithm)
{
    auto device_id = std::to_string(device_count) + "-" + std::to_string(vdevice_key);

    // If vdevice already exist, use it
    auto found_vdevice = get_vdevice(device_id, scheduling_algorithm);
    if (found_vdevice.status() != HAILO_NOT_FOUND) {
        GST_CHECK_EXPECTED(found_vdevice, element, RESOURCE, "Failed using shared vdevice, status = %d", found_vdevice.status());
        return found_vdevice.release();
    }

    hailo_vdevice_params_t params = {};
    params.device_count = device_count;
    params.device_infos = nullptr;
    params.scheduling_algorithm = scheduling_algorithm;
    auto vdevice = VDevice::create(params);
    GST_CHECK_EXPECTED(vdevice, element, RESOURCE, "Failed creating vdevice, status = %d", vdevice.status());
    std::shared_ptr<VDevice> vdevice_ptr = std::move(vdevice.release());

    m_shared_vdevices[device_id] = vdevice_ptr;
    m_shared_vdevices_scheduling_algorithm[device_id] = scheduling_algorithm;
    return vdevice_ptr;
}

Expected<std::shared_ptr<VDevice>> VDeviceManager::create_unique_vdevice(const void *element, uint16_t device_count,
    hailo_scheduling_algorithm_t scheduling_algorithm)
{
    hailo_vdevice_params_t params = {};
    params.device_count = device_count;
    params.device_infos = nullptr;
    params.scheduling_algorithm = scheduling_algorithm;
    auto vdevice = VDevice::create(params);
    GST_CHECK_EXPECTED(vdevice, element, RESOURCE, "Failed creating vdevice, status = %d", vdevice.status());

    // Unique vdevices are not saved in VDeviceManager, only in their hailonet
    std::shared_ptr<VDevice> vdevice_ptr = std::move(vdevice.release());
    m_unique_vdevices.push_back(vdevice_ptr);
    return vdevice_ptr;
}

Expected<std::shared_ptr<VDevice>> VDeviceManager::get_vdevice(const std::string &device_id,
    hailo_scheduling_algorithm_t scheduling_algorithm)
{
    auto found = m_shared_vdevices.find(device_id);
    if (found == m_shared_vdevices.end()) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    // shared_vdevice is found, verify the requested scheduling_algorithm
    assert(m_shared_vdevices_scheduling_algorithm.end() != m_shared_vdevices_scheduling_algorithm.find(device_id));
    if (scheduling_algorithm != m_shared_vdevices_scheduling_algorithm[device_id]) {
        auto status = HAILO_INVALID_OPERATION;
        g_warning("Shared vdevice with the same credentials is already exists (%s) but with a different scheduling-algorithm (requested: %d, exists: %d), status = %d",
            device_id.c_str(), scheduling_algorithm, m_shared_vdevices_scheduling_algorithm[device_id], status);
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    auto vdevice_cpy = found->second;
    return vdevice_cpy;
}

Expected<std::shared_ptr<ConfiguredNetworkGroup>> NetworkGroupConfigManager::configure_network_group(const void *element, const std::string &device_id,
    const char *network_group_name, uint16_t batch_size, std::shared_ptr<VDevice> &vdevice, std::shared_ptr<Hef> hef,
    NetworkGroupsParamsMap &net_groups_params_map)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    std::shared_ptr<ConfiguredNetworkGroup> found_cng = get_configured_network_group(device_id, network_group_name, batch_size);
    if (nullptr != found_cng) {
        return found_cng;
    }

    auto network_group_list = vdevice->configure(*hef, net_groups_params_map);
    GST_CHECK_EXPECTED(network_group_list, element, RESOURCE, "Failed configure device from hef, status = %d",
        network_group_list.status());

    std::shared_ptr<ConfiguredNetworkGroup> result = nullptr;
    for (auto &network_group : network_group_list.value()) {
        m_configured_net_groups[get_configure_string(device_id, network_group->get_network_group_name().c_str(), batch_size)] = network_group;
        if (std::string(network_group_name) == network_group->get_network_group_name()) {
            result = network_group;
            break;
        }
    }

    if (result) {
        return result;
    } else if (1 != network_group_list->size()) {
        g_error("Configuring HEF with multiple network_groups without providing valid network_group name. passed name = %s, status = %d", network_group_name, HAILO_NOT_FOUND);
        return make_unexpected(HAILO_NOT_FOUND);
    } else {
        return std::move(network_group_list->at(0));
    }
}

hailo_status NetworkGroupConfigManager::add_network_to_shared_network_group(const std::string &shared_device_id, const std::string &network_name,
    const GstElement *owner_element)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (shared_device_id.empty()) {
        // the device is unique so we don't need to share anything
        return HAILO_SUCCESS;
    }

    auto found_by_device = m_configured_networks.find(shared_device_id);
    if (found_by_device != m_configured_networks.end()) {
        auto found_network = found_by_device->second.find(network_name);
        GST_CHECK(found_network == found_by_device->second.end(), HAILO_INVALID_OPERATION, owner_element, RESOURCE,
            "Network %s was already configured by %s by the same device!", network_name.c_str(), found_network->second.c_str());
    }

    m_configured_networks[shared_device_id][network_name] = GST_ELEMENT_NAME(owner_element);
    return HAILO_SUCCESS;
}

std::shared_ptr<ConfiguredNetworkGroup> NetworkGroupConfigManager::get_configured_network_group(const std::string &device_id,
    const char *network_group_name, uint16_t batch_size)
{
    auto found = m_configured_net_groups.find(get_configure_string(device_id, network_group_name, batch_size));
    if (found == m_configured_net_groups.end()) {
        return nullptr;
    }

    return found->second;
}

std::string NetworkGroupConfigManager::get_configure_string(const std::string &device_id, const char *network_group_name, uint16_t batch_size)
{
    const char *EMPTY_FIELD = "NULL,";
    std::ostringstream oss;

    if (device_id.empty()) {
        oss << EMPTY_FIELD;
    } else {
        oss << device_id << ",";
    }

    if (nullptr == network_group_name) {
        oss << EMPTY_FIELD;
    } else {
        oss << network_group_name << ",";
    }

    oss << batch_size;
    return oss.str();
}

Expected<std::shared_ptr<ActivatedNetworkGroup>> NetworkGroupActivationManager::activate_network_group(const void *element, const std::string &device_id,
    const char *net_group_name, uint16_t batch_size, std::shared_ptr<ConfiguredNetworkGroup> cng)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    std::shared_ptr<ActivatedNetworkGroup> found_ang = get_activated_network_group(device_id, net_group_name, batch_size);
    if (nullptr != found_ang) {
        return found_ang;
    }

    auto activated_network_group = cng->activate();
    GST_CHECK_EXPECTED(activated_network_group, element, RESOURCE, "Failed activating network group, status = %d",
        activated_network_group.status());

    std::shared_ptr<ActivatedNetworkGroup> ang = std::move(activated_network_group.release());
    m_activated_net_groups[NetworkGroupConfigManager::get_configure_string(device_id, net_group_name, batch_size)] = ang;

    return ang;
}

std::shared_ptr<ActivatedNetworkGroup> NetworkGroupActivationManager::get_activated_network_group(const std::string &device_id,
    const char *net_group_name, uint16_t batch_size)
{
    auto found = m_activated_net_groups.find(NetworkGroupConfigManager::get_configure_string(device_id, net_group_name, batch_size));
    if (found == m_activated_net_groups.end()) {
        return nullptr;
    }

    return found->second;
}

hailo_status NetworkGroupActivationManager::remove_activated_network(const std::string &device_id, const char *net_group_name,
    uint16_t batch_size)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    auto found = m_activated_net_groups.find(NetworkGroupConfigManager::get_configure_string(device_id, net_group_name, batch_size));
    if (found == m_activated_net_groups.end()) {
        return HAILO_NOT_FOUND;
    }

    m_activated_net_groups.erase(found);
    return HAILO_SUCCESS;
}