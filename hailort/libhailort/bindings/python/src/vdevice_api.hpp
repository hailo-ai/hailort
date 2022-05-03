/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_api.hpp
 * @brief Defines binding to a VDevice class usage over Python.
 *
 * TODO: doc
 **/

#ifndef VDEVICE_API_HPP_
#define VDEVICE_API_HPP_

#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"

#include "utils.hpp"
#include "common/logger_macros.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>

#include <string>

namespace hailort
{

class VDeviceWrapper {
public:
    static VDeviceWrapper create(const hailo_vdevice_params_t &params)
    {
        return VDeviceWrapper(params);
    };

    static VDeviceWrapper create_from_infos(const std::vector<hailo_pcie_device_info_t> &device_infos)
    {
        hailo_vdevice_params_t params = {};
        params.device_count = static_cast<uint32_t>(device_infos.size());
        params.device_infos = const_cast<hailo_pcie_device_info_t*>(device_infos.data());
        return VDeviceWrapper(params);
    };

    VDeviceWrapper(const hailo_vdevice_params_t &params)
    {
        auto vdevice_expected = VDevice::create(params);
        VALIDATE_EXPECTED(vdevice_expected);

        m_vdevice = vdevice_expected.release();
    };

    py::list get_physical_devices_infos()
    {
        auto phys_devs_infos = m_vdevice->get_physical_devices_infos();
        VALIDATE_EXPECTED(phys_devs_infos);

        return py::cast(phys_devs_infos.value());
    };

    py::list configure(const HefWrapper &hef,
        const NetworkGroupsParamsMap &configure_params={})
    {

        auto network_groups = m_vdevice->configure(*hef.hef_ptr(), configure_params);
        VALIDATE_EXPECTED(network_groups);

        py::list results;
        for (const auto &network_group : network_groups.value()) {
            results.append(network_group.get());
        }

        return results;
    }

    void release()
    {
        m_vdevice.reset();
    }

private:
    std::unique_ptr<VDevice> m_vdevice;
};

void VDevice_api_initialize_python_module(py::module &m)
{
    py::class_<VDeviceWrapper>(m, "VDevice")
        .def("create", &VDeviceWrapper::create)
        .def("create_from_infos", &VDeviceWrapper::create_from_infos)
        .def("get_physical_devices_infos", &VDeviceWrapper::get_physical_devices_infos)
        .def("configure", &VDeviceWrapper::configure)
        .def("release", &VDeviceWrapper::release)
        ;
}

} /* namespace hailort */

#endif /* VDEVICE_API_HPP_ */
