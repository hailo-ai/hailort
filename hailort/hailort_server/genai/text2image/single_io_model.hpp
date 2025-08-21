/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file single_io_model.hpp
 * @brief Single input-Single output model manager
 **/

#ifndef _HAILO_SINGLE_IO_MODEL_HPP_
#define _HAILO_SINGLE_IO_MODEL_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/vdevice.hpp"
#include "common/utils.hpp"

#include "inference_manager.hpp"
#include "utils.hpp"

namespace hailort
{
namespace genai
{

// HRT-16908 - Create element hierarchy
class SingleIOModel final
{
public:
    // Note: We assume here that all the tpyes in the Text2Image pipeline, both input and outputs, are float32.
    // TODO: Allow support for other types
    static Expected<std::unique_ptr<SingleIOModel>> create(Hef hef, std::shared_ptr<hailort::VDevice> vdevice,
        hailo_format_type_t input_format_type, hailo_format_type_t output_format_type);

    virtual hailo_status execute();

    virtual hailo_status set_input_buffer(MemoryView input_buffer);
    virtual hailo_status set_output_buffer(MemoryView output_buffer);

    Expected<MemoryView> allocate_input_buffer();
    Expected<MemoryView> allocate_output_buffer();

    Expected<MemoryView> get_input_memview();
    Expected<MemoryView> get_output_memview();

    Expected<size_t> get_input_frame_size();
    Expected<size_t> get_output_frame_size();

    std::shared_ptr<InferModel> get_model();

    SingleIOModel(std::unique_ptr<InferenceManager> &&inference_manager);
    SingleIOModel(SingleIOModel &&) = delete;
    SingleIOModel(const SingleIOModel &) = delete;
    SingleIOModel &operator=(SingleIOModel &&) = delete;
    SingleIOModel &operator=(const SingleIOModel &) = delete;
    virtual ~SingleIOModel() = default;

protected:
    std::unique_ptr<InferenceManager> m_inference_manager;
    
    MemoryView m_input_memview;
    MemoryView m_output_memview;

    // Are filled if allocated by the element, else are nullptr.
    BufferPtr m_input_buffer;
    BufferPtr m_output_buffer;
};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_SINGLE_IO_MODEL_HPP_ */
