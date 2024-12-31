/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file measurement_utils.cpp
 * @brief Measurement utils module implementation
 **/

#include "hailo/hailort.h"
#include "measurement_utils.hpp"
#include <algorithm>


namespace hailort {
namespace utils {

const ITimeProvider& HighResTimeProvider::get_instance()
{
    static HighResTimeProvider instance;
    return instance;
}

HighResTimeProvider::time_point HighResTimeProvider::now() const
{
    return std::chrono::high_resolution_clock::now();
}

hailo_status MeasurementStorage::add_measurement(const std::string &accumulator_name, MeasurementType type,
    double measurement)
{
    return get_instance().add_measurement_impl(accumulator_name, type, measurement);
}

Expected<AccumulatorResults> MeasurementStorage::get_measurements(MeasurementType type, const std::string &accumulator_name)
{
    return get_instance().get_measurements_impl(type, accumulator_name);
}

void MeasurementStorage::set_verbosity(bool verbosity)
{
    return get_instance().set_verbosity_impl(verbosity);
}

void MeasurementStorage::set_precision(uint32_t precision)
{
    return get_instance().set_precision_impl(precision);
}

void MeasurementStorage::clear()
{
    return get_instance().clear_impl();
}

void MeasurementStorage::show_output_on_destruction(bool show_output)
{
    return get_instance().show_output_on_destruction_impl(show_output);
}

MeasurementStorage::~MeasurementStorage()
{
    if (!m_show_output_on_destruction) {
        return;
    }

    // Since MeasurementStorage has only one static instance, the following will be printed on program shutdown
    std::cout << "**** MEASUREMENT UTIL RESULTS ****\n";
    format_measurements(std::cout, MeasurementType::TIME);
    format_measurements(std::cout, MeasurementType::FPS);
    format_measurements(std::cout, MeasurementType::VALUE);
}

MeasurementStorage& MeasurementStorage::get_instance()
{
    static MeasurementStorage instance;
    return instance;
}

std::string MeasurementStorage::indent_string(const std::string &str, uint8_t indent_level)
{
    static const std::string INDENT = "    ";

    std::stringstream stream;
    for (auto i = 0; i < indent_level; i++) {
        stream << INDENT;
    }

    stream << str;
    return stream.str();
}

MeasurementStorage::AccumulatorMap &MeasurementStorage::get_storage(MeasurementType type)
{
    switch (type)
    {
    case MeasurementType::TIME:
        return m_time_acc_storage;
    case MeasurementType::FPS:
        return m_fps_acc_storage;
    case MeasurementType::VALUE:
        return m_value_acc_storage;
    default:
        // We should never get here, we'll return the time storage to avoid a crash
        LOGGER__ERROR("Invalid measurement type");
        return m_time_acc_storage;
    }
}

std::vector<std::pair<std::string, AccumulatorPtr>> MeasurementStorage::get_sorted_elements(MeasurementType type)
{
    // Storage is unordered in order to be as fast as possible in add_measurement
    // We now copy the elements to a vector and sort in order to get the most readable results
    // Note that we return a snapshot of the storage elements, and after this function returns the storage may change
    std::vector<std::pair<std::string, AccumulatorPtr>> sorted_accumulator_name_pairs;
    {
        auto &storage = get_storage(type);
        std::lock_guard<std::mutex> lock_guard(storage.mutex);

        sorted_accumulator_name_pairs.reserve(storage.map.size());
        sorted_accumulator_name_pairs.insert(sorted_accumulator_name_pairs.end(), storage.map.cbegin(), storage.map.cend());
    }
    std::sort(sorted_accumulator_name_pairs.begin(), sorted_accumulator_name_pairs.end());

    return sorted_accumulator_name_pairs;
}

std::string MeasurementStorage::get_measurement_title(MeasurementType type)
{
    switch (type)
    {
    case MeasurementType::TIME:
        return "Time measurements (ms)";
    case MeasurementType::FPS:
        return "FPS measurements";
    case MeasurementType::VALUE:
        return "Value measurements";
    default:
        // We should never get here
        LOGGER__ERROR("Invalid measurement type");
        return "Invalid measurement type";
    }
}

void MeasurementStorage::format_measurements(std::ostream &output_stream, MeasurementType type)
{
    static const std::string LIST_MARKER = "- ";

    const auto sorted_elements = get_sorted_elements(type);

    output_stream << indent_string(LIST_MARKER, 1)
                  << get_measurement_title(type) << ": ";
    if (sorted_elements.empty()) {
        output_stream << "No measurements";
    }
    output_stream << "\n";

    for (const auto &accumulator_name_pair : sorted_elements) {
        const auto &accumulator_name = accumulator_name_pair.first;
        const auto &accumulator_results = accumulator_name_pair.second->get();
        output_stream << indent_string(LIST_MARKER, 2) << accumulator_name << ": "
                      << AccumulatorResultsHelper::format_results(accumulator_results, m_verbose, m_precision) << "\n";
    }
}

hailo_status MeasurementStorage::add_measurement_impl(const std::string &accumulator_name, MeasurementType type,
    double measurement)
{
    auto &storage = get_storage(type);
    std::lock_guard<std::mutex> lock_guard(storage.mutex);

    auto it = storage.map.find(accumulator_name);
    if (it == storage.map.end()) {
        AccumulatorPtr accumulator = nullptr;
        if (MeasurementType::FPS == type) {
            accumulator = make_shared_nothrow<AverageFPSAccumulator<double>>(accumulator_name);
        } else {
            accumulator = make_shared_nothrow<FullAccumulator<double>>(accumulator_name);
        }
        CHECK_NOT_NULL(accumulator, HAILO_OUT_OF_HOST_MEMORY);
        storage.map[accumulator_name] = accumulator;
    }

    storage.map[accumulator_name]->add_data_point(measurement);
    return HAILO_SUCCESS;
}

Expected<AccumulatorResults> MeasurementStorage::get_measurements_impl(MeasurementType type, const std::string &accumulator_name)
{
    auto &storage = get_storage(type);
    std::lock_guard<std::mutex> lock_guard(storage.mutex);

    auto it = storage.map.find(accumulator_name);
    CHECK(it != storage.map.end(), HAILO_NOT_FOUND);

    return it->second->get();
}

void MeasurementStorage::set_verbosity_impl(bool verbosity)
{
    m_verbose = verbosity;
}

void MeasurementStorage::set_precision_impl(uint32_t precision)
{
    m_precision = precision;
}

void MeasurementStorage::clear_impl()
{
    // Note: After a certain storage is cleared, it could be filled again with new measurements
    //       We lock to avoid race conditions for a given map, not to make this function "atomic"
    for (auto &storage : {&m_time_acc_storage, &m_fps_acc_storage, &m_value_acc_storage}) {
        std::lock_guard<std::mutex> lock_guard(storage->mutex);
        storage->map.clear();
    }
}

void MeasurementStorage::show_output_on_destruction_impl(bool show_output)
{
    m_show_output_on_destruction = show_output;
}

} /* namespace utils */
} /* namespace hailort */
