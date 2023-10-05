/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_api.hpp
 * @brief Defines binding to a VDevice class usage over Python.
 **/

#ifndef VDEVICE_API_HPP_
#define VDEVICE_API_HPP_

#include "utils.hpp"
#include "network_group_api.hpp"

#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hailort_common.hpp"

#include <iostream>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>

#include <string>


namespace hailort
{

struct VDeviceParamsWrapper {
    hailo_vdevice_params_t orig_params;
    std::string group_id_str;
};


class VDeviceWrapper;
using VDeviceWrapperPtr = std::shared_ptr<VDeviceWrapper>;

class VDeviceWrapper {
public:
    static VDeviceWrapperPtr create(const hailo_vdevice_params_t &params)
    {
        return std::make_shared<VDeviceWrapper>(params);
    };

    static VDeviceWrapperPtr create(const VDeviceParamsWrapper &params)
    {
        return std::make_shared<VDeviceWrapper>(params.orig_params);
    }

    static VDeviceWrapperPtr create(const VDeviceParamsWrapper &params, const std::vector<std::string> &device_ids)
    {
        if (params.orig_params.device_ids != nullptr && (!device_ids.empty())) {
            std::cerr << "VDevice device_ids can be set in params or device_ids argument. Both parameters were passed to the c'tor";
            throw HailoRTStatusException(std::to_string(HAILO_INVALID_OPERATION));
        }
        auto modified_params = params;
        auto device_ids_vector = HailoRTCommon::to_device_ids_vector(device_ids);
        VALIDATE_EXPECTED(device_ids_vector);
        modified_params.orig_params.device_ids = device_ids_vector->data();
        return std::make_shared<VDeviceWrapper>(modified_params.orig_params);
    }

    static VDeviceWrapperPtr create_from_ids(const std::vector<std::string> &device_ids)
    {
        auto device_ids_vector = HailoRTCommon::to_device_ids_vector(device_ids);
        VALIDATE_EXPECTED(device_ids_vector);

        hailo_vdevice_params_t params = {};
        auto status = hailo_init_vdevice_params(&params);
        VALIDATE_STATUS(status);

        params.device_ids = device_ids_vector->data();
        params.device_count = static_cast<uint32_t>(device_ids_vector->size());
        params.scheduling_algorithm = HAILO_SCHEDULING_ALGORITHM_NONE;

        return std::make_shared<VDeviceWrapper>(params);
    }

    VDeviceWrapper(const hailo_vdevice_params_t &params)
#ifdef HAILO_IS_FORK_SUPPORTED
        :
        m_atfork_guard(this, {
            .before_fork = [this]() { if (m_vdevice) m_vdevice->before_fork(); },
            .after_fork_in_parent = [this]() { if (m_vdevice) m_vdevice->after_fork_in_parent(); },
            .after_fork_in_child = [this]() { if (m_vdevice) m_vdevice->after_fork_in_child(); },
        })
#endif
    {
        auto vdevice_expected = VDevice::create(params);
        VALIDATE_EXPECTED(vdevice_expected);

        m_vdevice = vdevice_expected.release();
    };

    py::list get_physical_devices_ids() const
    {
        const auto phys_devs_ids = m_vdevice->get_physical_devices_ids();
        VALIDATE_EXPECTED(phys_devs_ids);

        return py::cast(phys_devs_ids.value());
    }

    py::list configure(const HefWrapper &hef,
        const NetworkGroupsParamsMap &configure_params={})
    {
        auto network_groups = m_vdevice->configure(*hef.hef_ptr(), configure_params);
        VALIDATE_EXPECTED(network_groups);

        py::list results;
        m_net_groups.reserve(m_net_groups.size() + network_groups->size());
        for (const auto &network_group : network_groups.value()) {
            auto wrapper = ConfiguredNetworkGroupWrapper::create(network_group);
            results.append(wrapper);
            m_net_groups.emplace_back(wrapper);
        }

        return results;
    }

    void release()
    {
        m_net_groups.clear();
        m_vdevice.reset();
    }

private:
    std::unique_ptr<VDevice> m_vdevice;
    std::vector<ConfiguredNetworkGroupWrapperPtr> m_net_groups;

#ifdef HAILO_IS_FORK_SUPPORTED
    AtForkRegistry::AtForkGuard m_atfork_guard;
#endif
};

void VDevice_api_initialize_python_module(py::module &m)
{
    py::class_<VDeviceWrapper, VDeviceWrapperPtr>(m, "VDevice")
        .def("create", py::overload_cast<const hailo_vdevice_params_t&>(&VDeviceWrapper::create))
        .def("create", py::overload_cast<const VDeviceParamsWrapper&>(&VDeviceWrapper::create))
        .def("create", py::overload_cast<const VDeviceParamsWrapper&, const std::vector<std::string>&>(&VDeviceWrapper::create))
        .def("create_from_ids", &VDeviceWrapper::create_from_ids)
        .def("get_physical_devices_ids", &VDeviceWrapper::get_physical_devices_ids)
        .def("configure", &VDeviceWrapper::configure)
        .def("release", &VDeviceWrapper::release)
        ;
}

} /* namespace hailort */

#endif /* VDEVICE_API_HPP_ */
