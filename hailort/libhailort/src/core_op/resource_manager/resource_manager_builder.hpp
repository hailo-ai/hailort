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

#include "core_op/resource_manager/resource_manager.hpp"


namespace hailort
{

class ShefFileHandle;
class ResourcesManagerBuilder final {
public:
    ResourcesManagerBuilder() = delete;

    static Expected<std::shared_ptr<ResourcesManager>> build(uint8_t net_group_index, VdmaDevice &device,
        HailoRTDriver &driver, const ConfigureNetworkParams &config_params,
        std::shared_ptr<CoreOpMetadata> core_op, const HEFHwArch &hw_arch, std::shared_ptr<ShefFileHandle> shef_file_handle);

};

} /* namespace hailort */

#endif /* _HAILO_RESOURCE_MANAGER_BUILDER_HPP_ */
