/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measure_nnc_performance_command.hpp
 * @brief measure nerual network performance for given network using only the HW components without host SW
 **/

#ifndef _HAILO_HW_INFER_ESTIMATOR_COMMAND_HPP_
#define _HAILO_HW_INFER_ESTIMATOR_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "CLI/CLI.hpp"

struct hw_infer_runner_params {
    hailo_vdevice_params vdevice_params;
    std::string hef_path;
    uint16_t batch_size;
    hailo_power_mode_t power_mode;
};

class HwInferEstimatorCommand : public Command {
public:
    explicit HwInferEstimatorCommand(CLI::App &parent_app);
    hailo_status execute() override;

private:
    hw_infer_runner_params m_params;
};

#endif /*_HAILO_HW_INFER_ESTIMATOR_COMMAND_HPP_*/