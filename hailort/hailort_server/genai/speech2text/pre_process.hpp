/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pre_process.hpp
 * @brief Speech2Text pre-process functions
 **/

#ifndef _HAILO_PRE_PROCESS_HPP_
#define _HAILO_PRE_PROCESS_HPP_

#include "hailo/buffer.hpp"
#include "hailo/hailort.h"
#include "eigen.hpp"

namespace hailort
{
namespace genai
{

// TODO: HRT-18570 - Consider removing this class or changing to audio utils
class Speech2TextPreProcess
{
public:
    Speech2TextPreProcess() = delete;

    static Eigen::MatrixXf compute_log_mel(const MemoryView chunk);
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_PRE_PROCESS_HPP_ */
