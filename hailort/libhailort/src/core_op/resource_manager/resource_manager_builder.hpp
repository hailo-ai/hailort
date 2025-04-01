/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file resource_manager_builder.hpp
 * @brief Builds a ResourcesManager object for the given CoreOp.
 **/

#ifndef _HAILO_RESOURCE_MANAGER_BUILDER_HPP_
#define _HAILO_RESOURCE_MANAGER_BUILDER_HPP_

#include "core_op/resource_manager/resource_manager.hpp"
#include "core_op/resource_manager/cache_manager.hpp"


namespace hailort
{

class Reader;
class ResourcesManagerBuilder final {
public:
    ResourcesManagerBuilder() = delete;

    static Expected<std::shared_ptr<ResourcesManager>> build(uint8_t net_group_index, VdmaDevice &device,
        HailoRTDriver &driver, CacheManagerPtr cache_manager, const ConfigureNetworkParams &config_params,
        std::shared_ptr<CoreOpMetadata> core_op, const HEFHwArch &hw_arch, const Hef &hef);

    static hailo_status prepare_aligned_ccws_resources(const Hef &hef, ResourcesManager &resources_manager, HailoRTDriver &driver);

};

} /* namespace hailort */

#endif /* _HAILO_RESOURCE_MANAGER_BUILDER_HPP_ */
