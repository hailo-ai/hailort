/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#ifndef _NETWORK_GROUP_HANDLE_HPP_
#define _NETWORK_GROUP_HANDLE_HPP_

#include "common.hpp"
#include "hailo_events/hailo_events.hpp"
#include "hailo/vdevice.hpp"

#include <unordered_map>
#include <mutex>
#include <unordered_set>

using device_id_t = std::string;
using network_name_t = std::string;
using sync_hailonet_name_t = std::string;

class NetworkGroupConfigManager final
{
public:
    NetworkGroupConfigManager() : m_configured_net_groups() {}
    Expected<std::shared_ptr<ConfiguredNetworkGroup>> configure_network_group(const void *element, const std::string &device_id,
        hailo_scheduling_algorithm_t scheduling_algorithm, const char *network_group_name, uint16_t batch_size, std::shared_ptr<VDevice> &vdevice, std::shared_ptr<Hef> hef,
        NetworkGroupsParamsMap &net_groups_params_map);
    hailo_status add_network_to_shared_network_group(const std::string &shared_device_id, const std::string &network_name,
        const GstElement *owner_element);
    
private:
    static std::string get_configure_string(const std::string &device_id, const std::string &hef_hash,
        const char *network_group_name, uint16_t batch_size);
    friend class NetworkGroupActivationManager;

    std::shared_ptr<ConfiguredNetworkGroup> get_configured_network_group(const std::string &device_id, const std::string &hef_hash,
        const char *net_group_name, uint16_t batch_size);

    // TODO: change this map to store only the shared network_groups (used by multiple hailonets with the same vdevices)
    std::unordered_map<std::string, std::weak_ptr<ConfiguredNetworkGroup>> m_configured_net_groups;
    std::unordered_map<device_id_t, std::unordered_map<network_name_t, sync_hailonet_name_t>> m_configured_networks;
    std::mutex m_mutex;
};

class NetworkGroupActivationManager final
{
public:
    NetworkGroupActivationManager() : m_activated_net_groups() {}
    Expected<std::shared_ptr<ActivatedNetworkGroup>> activate_network_group(const void *element, const std::string &device_id,
        const std::string &hef_hash, const char *net_group_name, uint16_t batch_size, std::shared_ptr<ConfiguredNetworkGroup> cng);
    hailo_status remove_activated_network(const std::string &device_id, const std::string &hef_hash, const char *net_group_name, uint16_t batch_size);
    
private:
    std::shared_ptr<ActivatedNetworkGroup> get_activated_network_group(const std::string &device_id, const std::string &hef_hash,
        const char *net_group_name, uint16_t batch_size);

    // TODO: change this map to store only the shared network_groups (used by multiple hailonets with the same vdevices)
    std::unordered_map<std::string, std::shared_ptr<ActivatedNetworkGroup>> m_activated_net_groups;
    std::mutex m_mutex;
};

class NetworkGroupHandle final
{
public:
    NetworkGroupHandle(const GstElement *element) : m_element(element), m_shared_device_id(), m_net_group_name(), m_network_name(), m_batch_size(0),
        m_vdevice(nullptr), m_hef(nullptr), m_cng(nullptr), m_ang(nullptr) {}

    hailo_status set_hef(const char *device_id, uint16_t device_count, uint32_t vdevice_key, hailo_scheduling_algorithm_t scheduling_algorithm,
        bool multi_process_service, const char *hef_path);
    hailo_status configure_network_group(const char *net_group_name, hailo_scheduling_algorithm_t scheduling_algorithm, uint16_t batch_size);
    Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> create_vstreams(const char *network_name,
        hailo_scheduling_algorithm_t scheduling_algorithm, const std::vector<hailo_format_with_name_t> &output_formats,
        hailo_format_type_t input_format_type, hailo_format_type_t output_format_type);
    hailo_status activate_network_group();
    Expected<bool> remove_network_group();

    hailo_status set_scheduler_timeout(const char *network_name, uint32_t timeout_ms);
    hailo_status set_scheduler_threshold(const char *network_name, uint32_t threshold);
    hailo_status set_scheduler_priority(const char *network_name, uint8_t priority);

    std::shared_ptr<Hef> hef()
    {
        return m_hef;
    }

private:
    Expected<NetworkGroupsParamsMap> get_configure_params(Hef &hef, const VDevice &vdevice, const char *net_group_name,
        uint16_t batch_size);
    static Expected<std::shared_ptr<VDevice>> create_vdevice(const void *element, const std::string &device_id, uint16_t device_count,
        uint32_t vdevice_key, hailo_scheduling_algorithm_t scheduling_algorithm, bool multi_process_service);
    Expected<std::shared_ptr<VDevice>> create_vdevice(const std::string &device_id, uint16_t device_count, uint32_t vdevice_key,
        hailo_scheduling_algorithm_t scheduling_algorithm, bool multi_process_service);

    static std::unordered_set<std::shared_ptr<VDevice>> m_vdevices;
    static NetworkGroupConfigManager m_net_group_config_manager;
    static NetworkGroupActivationManager m_net_group_activation_manager;
    const GstElement *m_element;
    std::string m_shared_device_id; // empty string when using unique device
    std::string m_net_group_name;
    std::string m_network_name;
    uint16_t m_batch_size;
    std::shared_ptr<VDevice> m_vdevice;
    std::shared_ptr<Hef> m_hef;
    std::shared_ptr<ConfiguredNetworkGroup> m_cng;
    std::shared_ptr<ActivatedNetworkGroup> m_ang;
};

#endif /* _NETWORK_GROUP_HANDLE_HPP_ */