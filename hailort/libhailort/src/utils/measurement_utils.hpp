/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measurement_utils.hpp
 * @brief This module provides utility classes for measuring and storing runtime statistics of designated code
 *        blocks/functions.
 *        Three classes are provided for measurements:
 *        1) utils::MeasureTime - measures the execution time of the scope in which its declared
 *        2) utils::MeasureFps - measures the fps of the scope in which its declared
 *        3) utils::MeasureValue - measures a numeric value
 *
 *        Usage:
 *        1) To measure the running time of a certain function, declare an instance of utils::MeasureTime at the start
 *           of the function. E.g.
 *               1  hailo_status BoundaryChannel::inc_num_available(uint16_t value)
 *               2  {
 *               3      utils::MeasureTime time("inc_num_available on channel_id={}", m_channel_id.channel_index);
 *               4      // ...
 *               5      return m_host_registers.set_num_available(static_cast<uint16_t>(num_available));
 *               6  }
 *           The MEASURE_TIME macro can be used to simplify the declaration of MeasureTime instances. E.g.
 *           Replace line 3 in the above example with:
 *           MEASURE_TIME("inc_num_available on channel_id={}", m_channel_id.channel_index);
 *        2) To measure the FPS of a certain function use utils::MeasureFps or the MEASURE_FPS macro.
 *           The usage is the same as utils::MeasureTime/MEASURE_TIME.
 *        3) In some cases we'll want to only measure the performance-critical section of the function. In this case,
 *           open a new scope surrounding this section, and declare an instance of MeasureTime at the start of it. E.g.
 *               1  hailo_status BoundaryChannel::prepare_descriptors(..., MappedBufferPtr mapped_buffer, ...)
 *               2  {
 *               3      if (mapped_buffer != nullptr) {
 *               4          // Code that we don't want to measure...
 *               5          if (!is_buffer_already_configured(mapped_buffer, buffer_offset_in_descs, starting_desc)) {
 *               6              // More code that we don't want to measure...
 *               7              {
 *               8                  // We wrapped configure_to_use_buffer with a new scope, because we only want to measure it
 *               9                  // (originally it wasn't in it's own scope)
 *               10                 utils::MeasureTime time("configure_to_use_buffer on channel_id={}", m_channel_id.channel_index);
 *               11                 auto status = m_desc_list->configure_to_use_buffer(*mapped_buffer, m_channel_id, configure_starting_desc);
 *               12                 CHECK_SUCCESS(status);
 *               13             }
 *               14         }
 *               15     }
 *               16     // More code...
 *               17     return HAILO_SUCCESS;
 *               18 }
 *           Again, the MEASURE_TIME macro can be used in place of the MeasureTime declaration.
 *        4) To measure the FPS of a certain section use utils::MeasureFps or MEASURE_TIME.
 *           The usage is the same as utils::MeasureTime/MEASURE_TIME.
 *        5) To measure a numeric value, use the MEASURE_VALUE macro. E.g.
 *               1  hailo_status CoreOpsScheduler::switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
 *               2  {
 *               3      // ...
 *               4      auto hw_batch_size = scheduled_core_op->use_dynamic_batch_flow() ? frames_count : SINGLE_CONTEXT_BATCH_SIZE;
 *               5      MEASURE_VALUE(hw_batch_size, "core_op_handle={}", core_op_handle);
 *               6      // ...
 *               7  }
 *           The MEASURE_VALUE macro simplifies the declaration of MeasureValue instances (the class that implements
 *           the measurement logic), and it's usage is preferred. The macro will use the stringified variable name as
 *           the prefix for the accumulator name.
 *           E.g. for core_op_handle=0 the accumulator name will be "hw_batch_size (core_op_handle=0)".
 *        6) Be sure to provide a descriptive name for each measurement. In the above examples, channel_id was used in
 *           order to differentiate between set_num_available/configure_to_use_buffer on different channels.
 *        7) At the end of the program's execution, the measurements will be printed to stdout. For example, given the
 *           measurements registered in the examples provided for MeasureTime, the following will be printed upon
 *           hailortcli's completion:
 *           $ hailortcli run2 -m raw_async set-net shortcut_net_1080_1920_3.hef
 *           [===================>] 100% 00:00:00
 *           shortcut_net: fps: 255.72
 *           **** MEASUREMENT UTIL RESULTS ****
 *               - Time measurements (ms):
 *                   - configure_to_use_buffer on channel_id=1: count=1285, mean=0.2604
 *                   - configure_to_use_buffer on channel_id=16: count=1285, mean=0.2583
 *                   - inc_num_available on channel_id=1: count=1285, mean=0.0030
 *                   - inc_num_available on channel_id=16: count=1285, mean=0.0017
 *               - FPS measurements: No measurements
 *               - Value measurements: No measurements
 *
 *        Important note!
 *            The module is intended for debugging of performance bottlenecks. For "release-grade" performance
 *            monitoring use other classes provided in the library. For example, see references to AccumulatorPtr
 *            in the core_op modules or DurationCollector in the pipeline modules.
 **/

#ifndef _HAILO_MEASUREMENT_UTILS_HPP_
#define _HAILO_MEASUREMENT_UTILS_HPP_

#include "measurement_utils_internal.hpp"
#include <spdlog/fmt/bundled/format.h>

namespace hailort {
namespace utils {

// Measures the execution time of a block/function in milli-seconds
class MeasureTime : public MeasureTimeBase<std::milli>
{
public:
    MeasureTime(const ITimeProvider& time_provider, const std::string &accumulator_name) :
        MeasureTimeBase::MeasureTimeBase(MeasurementType::TIME, accumulator_name, time_provider)
    {}

    template <typename... Args>
    MeasureTime(const ITimeProvider& time_provider, const std::string &accumulator_name_format, Args&&... args) :
        MeasureTime(time_provider, fmt::format(accumulator_name_format, std::forward<Args>(args)...))
    {}

    MeasureTime(const std::string &accumulator_name) :
        MeasureTime(HighResTimeProvider::get_instance(), accumulator_name)
    {}

    template <typename... Args>
    MeasureTime(const std::string &accumulator_name_format, Args&&... args) :
        MeasureTime(HighResTimeProvider::get_instance(), fmt::format(accumulator_name_format, std::forward<Args>(args)...))
    {}
};

// Measures the fps of a block/function
// Using ratio<1,1> so that time measurements will be in seconds (needed for correct fps units)
class MeasureFps : public MeasureTimeBase<std::ratio<1,1>>
{
public:
    MeasureFps(const ITimeProvider& time_provider, const std::string &accumulator_name) :
        MeasureTimeBase::MeasureTimeBase(MeasurementType::FPS, accumulator_name, time_provider)
    {}

    template <typename... Args>
    MeasureFps(const ITimeProvider& time_provider, const std::string &accumulator_name_format, Args&&... args) :
        MeasureFps(time_provider, fmt::format(accumulator_name_format, std::forward<Args>(args)...))
    {}

    MeasureFps(const std::string &accumulator_name) :
        MeasureFps(HighResTimeProvider::get_instance(), accumulator_name)
    {}

    template <typename... Args>
    MeasureFps(const std::string &accumulator_name_format, Args&&... args) :
        MeasureFps(HighResTimeProvider::get_instance(), fmt::format(accumulator_name_format, std::forward<Args>(args)...))
    {}
};

// Measures a numeric value
template<typename T, std::enable_if_t<std::is_arithmetic<T>::value, int> = 0>
class MeasureValue : public Measure
{
public:
    MeasureValue(T value, const std::string &accumulator_name) :
        Measure::Measure(MeasurementType::VALUE, accumulator_name)
    {
        m_measurement = static_cast<double>(value);
    }

    template <typename... Args>
    MeasureValue(T value, const std::string &accumulator_name_format, Args&&... args) :
        MeasureValue(value, fmt::format(accumulator_name_format, std::forward<Args>(args)...))
    {}
};

// TODO: The helper macros are only available for GCC because of ##__VA_ARGS__ support (HRT-13031)
#ifdef __GNUC__
#define _CONCAT_HELPER(x, y) x##y
#define _CONCAT(x, y) _CONCAT_HELPER(x, y)

// Helper macro for measuring the execution time of a block/function
// Note: An instance with a unique name will be created (__time_<lineno>), so that:
//       a) the measurements will be completed at the end of the scope
//       b) name shadowing will be avoided
//       c) If time_provider is passed, it will be used for the measurements
#define MEASURE_TIME(accumulator_name_format, ...) \
    hailort::utils::MeasureTime _CONCAT(__time_, __LINE__)(accumulator_name_format, ##__VA_ARGS__)

#define MEASURE_TIME_WITH_PROVIDER(time_provider, accumulator_name_format, ...) \
    hailort::utils::MeasureTime _CONCAT(__time_, __LINE__)(time_provider, accumulator_name_format, ##__VA_ARGS__)

// Helper macro for measuring fps of a block/function
// Note: An instance with a unique name will be created (__time_<lineno>), so that:
//       a) the measurements will be completed at the end of the scope
//       b) name shadowing will be avoided
//       c) If time_provider is passed, it will be used for the measurements
#define MEASURE_FPS(accumulator_name_format, ...) \
    hailort::utils::MeasureFps _CONCAT(__time_, __LINE__)(accumulator_name_format, ##__VA_ARGS__)

#define MEASURE_FPS_WITH_PROVIDER(time_provider, accumulator_name_format, ...) \
    hailort::utils::MeasureFps _CONCAT(__time_, __LINE__)(time_provider, accumulator_name_format, ##__VA_ARGS__)

// Helper macro for measuring a numeric value
// Note: The accumulator's format is the stringified variable name together with accumulator_name_format.
//       E.g. calling MEASURE_VALUE(hw_batch_size, "core_op_handle={}", core_op_handle) with core_op_handle=0 will
//       yield the accumulator name "hw_batch_size (core_op_handle={})".
// Note: The MeasureValue instances created here are temporary. Unlike MeasureTime and MeasureFps,
//       we measure the value right away and not at the end of a scope.
#define MEASURE_VALUE(value, accumulator_name_format, ...) \
    hailort::utils::MeasureValue<decltype(value)>((value), #value " (" accumulator_name_format ")", ##__VA_ARGS__)

#endif /* __GNUC__ */

} /* namespace utils */
} /* namespace hailort */

#endif /* _HAILO_MEASUREMENT_UTILS_HPP_ */
