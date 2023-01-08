/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file resource_manager_builder.hpp
 * @brief Builds a ResourcesManager object for the given CoreOp.
 **/

#ifndef _HAILO_RESOURCE_MANAGER_BUILDER_HPP_
#define _HAILO_RESOURCE_MANAGER_BUILDER_HPP_

#include "hef_internal.hpp"
#include "context_switch/multi_context/resource_manager.hpp"


namespace hailort
{

class ResourcesManagerBuilder final {
public:
    ResourcesManagerBuilder() = delete;

    /* TODO HRT-5067 - work with hailo_device_architecture_t instead of ProtoHEFHwArch */
    static Expected<std::shared_ptr<ResourcesManager>> build(uint8_t net_group_index, VdmaDevice &device,
        HailoRTDriver &driver, const ConfigureNetworkParams &config_params,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata, const ProtoHEFHwArch &hw_arch);

};

} /* namespace hailort */

#endif /* _HAILO_RESOURCE_MANAGER_BUILDER_HPP_ */
