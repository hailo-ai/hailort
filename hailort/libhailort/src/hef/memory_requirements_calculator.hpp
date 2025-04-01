/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file memory_requirements_calculator.hpp
 * @brief Calculates the memory requirements used for running one or more models
 **/
#ifndef _HAILO_MEMORY_REQUIREMENTS_CALCULATOR_HPP_
#define _HAILO_MEMORY_REQUIREMENTS_CALCULATOR_HPP_

#include "hailo/expected.hpp"
#include "hailo/hef.hpp"

#include <string>
#include <vector>


namespace hailort
{

struct EdgeTypeMemoryRequirements {
    // Amount of CMA memory (Physically continous) in bytes needed for execution
    size_t cma_memory;

    // Amount of CMA memory (Physically continous) in bytes needed for creating descriptors list.
    size_t cma_memory_for_descriptors;

    // Amount of pinned memory (Memory pinned to physical memory) in bytes needed for execution
    size_t pinned_memory;
};

// Memory requirements for one model
struct MemoryRequirements {
    EdgeTypeMemoryRequirements intermediate_buffers;
    EdgeTypeMemoryRequirements config_buffers;
};

// Memory requirements for several models
struct FullMemoryRequirements {
    std::vector<MemoryRequirements> hefs_memory_requirements;

    // Notice that the total memory requirements is not the sum of all the memory requirements, since some of the memory
    // is shared between the models.
    MemoryRequirements total_memory_requirements;
};

/**
 * Used to calculate the memory requirements for running one or more models.
 * This class is exported as an internal API to be used by internal tools (so the header is not public, but the code
 * is exported)
 */
class HAILORTAPI MemoryRequirementsCalculator final {
public:
    struct HefParams {
        std::string hef_path;
        std::string network_group_name;
        uint16_t batch_size;
    };

    static Expected<FullMemoryRequirements> get_memory_requirements(const std::vector<HefParams> &models);
};

} /* namespace hailort */

#endif /* _HAILO_MEMORY_REQUIREMENTS_CALCULATOR_HPP_ */
