/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_api.hpp
 * @brief Defines binding to network group
 **/

#ifndef _HAILO_NETWORK_GROUP_API_HPP_
#define _HAILO_NETWORK_GROUP_API_HPP_

#include "utils.hpp"
#include "vstream_api.hpp"

#include "common/fork_support.hpp"

#include "hailo/network_group.hpp"


namespace hailort
{

class ActivatedAppContextManagerWrapper final
{
public:
    ActivatedAppContextManagerWrapper(ConfiguredNetworkGroup &net_group,
        const hailo_activate_network_group_params_t &network_group_params);

    const ActivatedNetworkGroup& enter();
    void exit();
    static void bind(py::module &m);
private:
    std::unique_ptr<ActivatedNetworkGroup> m_activated_net_group;
    ConfiguredNetworkGroup &m_net_group;
    hailo_activate_network_group_params_t m_network_group_params;
};

class ConfiguredNetworkGroupWrapper;
using ConfiguredNetworkGroupWrapperPtr = std::shared_ptr<ConfiguredNetworkGroupWrapper>;

class ConfiguredNetworkGroupWrapper final {
public:

    static ConfiguredNetworkGroupWrapperPtr create(std::shared_ptr<ConfiguredNetworkGroup> cng)
    {
        return std::make_shared<ConfiguredNetworkGroupWrapper>(cng);
    }

    ConfiguredNetworkGroupWrapper(std::shared_ptr<ConfiguredNetworkGroup> cng, bool store_guard_for_multi_process = false) :
        m_cng(cng)
#ifdef HAILO_IS_FORK_SUPPORTED
        ,
        m_atfork_guard(this, {
            .before_fork = [this]() { before_fork(); },
            .after_fork_in_parent = [this]() { after_fork_in_parent(); },
            .after_fork_in_child = [this]() { after_fork_in_child(); }
        })
#endif
    {
        if (store_guard_for_multi_process) {
            m_cng_guard_for_mt = cng;
        }
    }

    auto is_scheduled()
    {
        return get().is_scheduled();
    }

    auto get_name()
    {
        return get().name();
    }

    auto get_default_streams_interface()
    {
        auto result = get().get_default_streams_interface();
        VALIDATE_EXPECTED(result);
        return result.value();
    }

    auto activate(const hailo_activate_network_group_params_t &network_group_params)
    {
        return ActivatedAppContextManagerWrapper(get(), network_group_params);
    }

    void wait_for_activation(uint32_t timeout_ms)
    {
        auto status = get().wait_for_activation(std::chrono::milliseconds(timeout_ms));
        if (status != HAILO_NOT_IMPLEMENTED) {
            VALIDATE_STATUS(status);
        }
    }

    auto InputVStreams(const std::map<std::string, hailo_vstream_params_t> &input_vstreams_params)
    {
        return InputVStreamsWrapper::create(get(), input_vstreams_params);
    }

    auto OutputVStreams(const std::map<std::string, hailo_vstream_params_t> &output_vstreams_params)
    {
        return OutputVStreamsWrapper::create(get(), output_vstreams_params);
    }

    void set_scheduler_timeout(int timeout, const std::string &network_name="")
    {
        auto timeout_mili = std::chrono::milliseconds(timeout);
        auto status = get().set_scheduler_timeout(timeout_mili, network_name);
        VALIDATE_STATUS(status);
    }

    void set_scheduler_threshold(uint32_t threshold)
    {
        auto status = get().set_scheduler_threshold(threshold);
        VALIDATE_STATUS(status);
    }

    void set_scheduler_priority(uint8_t priority)
    {
        auto status = get().set_scheduler_priority(priority);
        VALIDATE_STATUS(status);
    }

    void init_cache(uint32_t read_offset)
    {
        auto status = get().init_cache(read_offset);
        VALIDATE_STATUS(status);
    }

    void update_cache_offset(int32_t offset_delta_entries)
    {
        auto status = get().update_cache_offset(offset_delta_entries);
        VALIDATE_STATUS(status);
    }

    auto get_cache_ids()
    {
        auto ids = get().get_cache_ids();
        VALIDATE_EXPECTED(ids);
        return ids;
    }

    py::bytes read_cache_buffer(uint32_t cache_id)
    {
        auto buffer = get().read_cache_buffer(cache_id);
        VALIDATE_EXPECTED(buffer);
        return py::bytes(buffer->as_pointer<char>(), buffer->size());
    }

    void write_cache_buffer(uint32_t cache_id, py::bytes buffer)
    {
        auto buffer_str = std::string(buffer);
        auto buffer_view = MemoryView::create_const(buffer_str.data(), buffer_str.size());
        auto status = get().write_cache_buffer(cache_id, buffer_view);
        VALIDATE_STATUS(status);
    }

    auto get_networks_names()
    {
        auto network_infos = get().get_network_infos();
        VALIDATE_EXPECTED(network_infos);
        std::vector<std::string> result;
        result.reserve(network_infos->size());
        for (const auto &info : network_infos.value()) {
            result.push_back(info.name);
        }
        return py::cast(result);
    }

    auto get_sorted_output_names()
    {
        auto names_list = get().get_sorted_output_names();
        VALIDATE_EXPECTED(names_list);
        return py::cast(names_list.release());
    }

    auto get_input_vstream_infos(const std::string &name)
    {
        auto result = get().get_input_vstream_infos(name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.value());
    }

    auto get_output_vstream_infos(const std::string &name)
    {
        auto result = get().get_output_vstream_infos(name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.value());
    }

    auto get_all_vstream_infos(const std::string &name)
    {
        auto result = get().get_all_vstream_infos(name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.value());
    }

    auto get_all_stream_infos(const std::string &name)
    {
        auto result = get().get_all_stream_infos(name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.value());
    }

    auto get_input_stream_infos(const std::string &name)
    {
        std::vector<hailo_stream_info_t> input_streams_infos;
        auto all_streams = get().get_all_stream_infos(name);
        VALIDATE_EXPECTED(all_streams);
        for (auto &info : all_streams.value()) {
            if (HAILO_H2D_STREAM == info.direction) {
                input_streams_infos.push_back(std::move(info));
            }
        }
        return py::cast(input_streams_infos);
    }

    auto get_output_stream_infos(const std::string &name)
    {
        std::vector<hailo_stream_info_t> output_streams_infos;
        auto all_streams = get().get_all_stream_infos(name);
        VALIDATE_EXPECTED(all_streams);
        for (auto &info : all_streams.value()) {
            if (HAILO_D2H_STREAM == info.direction) {
                output_streams_infos.push_back(std::move(info));
            }
        }
        return py::cast(output_streams_infos);
    }

    auto get_vstream_names_from_stream_name(const std::string &stream_name)
    {
        auto result = get().get_vstream_names_from_stream_name(stream_name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.release());
    }

    auto get_stream_names_from_vstream_name(const std::string &vstream_name)
    {
        auto result = get().get_stream_names_from_vstream_name(vstream_name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.release());
    }

    auto make_input_vstream_params(const std::string &name, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size)
    {
        auto result = get().make_input_vstream_params({}, format_type, timeout_ms, queue_size, name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.release());
    }

    auto make_output_vstream_params(const std::string &name, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size)
    {
        auto result = get().make_output_vstream_params({}, format_type, timeout_ms, queue_size, name);
        VALIDATE_EXPECTED(result);
        return py::cast(result.release());
    }

    ConfiguredNetworkGroup &get()
    {
        auto cng = m_cng.lock();
        VALIDATE_NOT_NULL(cng, HAILO_INTERNAL_FAILURE);
        return *cng;
    }

    ConfiguredNetworkGroup &get() const
    {
        auto cng = m_cng.lock();
        VALIDATE_NOT_NULL(cng, HAILO_INTERNAL_FAILURE);
        return *cng;
    }

    void before_fork()
    {
        auto cng = m_cng.lock();
        if (cng) {
            cng->before_fork();
        }
    }

    void after_fork_in_parent()
    {
        auto cng = m_cng.lock();
        if (cng) {
            cng->after_fork_in_parent();
        }
    }

    void after_fork_in_child()
    {
        auto cng = m_cng.lock();
        if (cng) {
            cng->after_fork_in_child();
        }
    }

    static void bind(py::module &m);

private:
    // Normally, the ownership of the network group is the Device/VDevice objects. We keep weak_ptr
    // to force free the network group before freeing the device/vdevice.
    std::weak_ptr<ConfiguredNetworkGroup> m_cng;

    // On multi-process, when pickling this object (the windows multi-process flow) the device/vdevice
    // doesn't own the network group object.
    // To solve this problem, we store here and optional guard for the network group that will exist
    // only when the object is constructed with pickle.
    std::shared_ptr<ConfiguredNetworkGroup> m_cng_guard_for_mt;

#ifdef HAILO_IS_FORK_SUPPORTED
    AtForkRegistry::AtForkGuard m_atfork_guard;
#endif
};

void NetworkGroup_api_initialize_python_module(py::module &m);

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_API_HPP_ */
