/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file config_manager.hpp
 * @brief Manager of HEF parsing and network groups resources
 *
 *
 **/

#ifndef HAILO_CONFIG_MANAGER_HPP_
#define HAILO_CONFIG_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/expected.hpp"
#include "common/utils.hpp"

#include <vector>
#include <map>
#include <algorithm>

namespace hailort
{

enum class ConfigManagerType {
    NotSet = 0,
    HcpConfigManager = 1,
    VdmaConfigManager = 2,
};

class ConfigManager
{
  public:
    virtual ~ConfigManager() {}
    virtual ConfigManagerType get_manager_type() = 0;
    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const std::map<std::string,
        ConfigureNetworkParams> &configure_params) = 0;

  protected:
    hailo_status validate_boundary_streams_were_created(Hef &hef, const std::string &network_group_name, ConfiguredNetworkGroup &network_group)
    {
        auto number_of_inputs = hef.get_number_of_input_streams(network_group_name);
        CHECK_EXPECTED_AS_STATUS(number_of_inputs);
        CHECK((number_of_inputs.value() == network_group.get_input_streams().size()),
            HAILO_INVALID_ARGUMENT, "passed configure_params for network group {} did not contain all input streams", network_group_name);

        auto number_of_outputs = hef.get_number_of_output_streams(network_group_name);
        CHECK_EXPECTED_AS_STATUS(number_of_inputs);
        CHECK((number_of_outputs.value() == network_group.get_output_streams().size()),
            HAILO_INVALID_ARGUMENT, "passed configure_params for network group {} did not contain all output streams", network_group_name);
        
        return HAILO_SUCCESS;
    }
};

} /* namespace hailort */

#endif /* HAILO_CONFIG_MANAGER_HPP_ */