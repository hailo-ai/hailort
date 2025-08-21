/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file single_io_model.cpp
 * @brief Model with single input-single output
 **/

#include "single_io_model.hpp"
#include "hailo/hailort.h"

namespace hailort
{
namespace genai
{

Expected<std::unique_ptr<SingleIOModel>> SingleIOModel::create(Hef hef, std::shared_ptr<hailort::VDevice> vdevice,
    hailo_format_type_t input_format_type, hailo_format_type_t output_format_type)
{
    TRY(auto inference_manager, InferenceManager::create(vdevice, hef));

    TRY(auto input, inference_manager->get_model()->input());
    TRY(auto output, inference_manager->get_model()->output());
    input.set_format_type(input_format_type);
    output.set_format_type(output_format_type);
    CHECK_SUCCESS(inference_manager->configure());

    auto ptr = make_unique_nothrow<SingleIOModel>(std::move(inference_manager));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY); // Consider returning different status

    return ptr;
}

SingleIOModel::SingleIOModel(std::unique_ptr<InferenceManager> &&inference_manager) :
    m_inference_manager(std::move(inference_manager))
{}

hailo_status SingleIOModel::set_input_buffer(MemoryView input_buffer)
{
    CHECK(m_input_buffer == nullptr, HAILO_INVALID_OPERATION, "Failed to set input buffer, buffer was already allocated.");

    TRY(auto input, m_inference_manager->get_model()->input());
    CHECK(input_buffer.size() == input.get_frame_size(), HAILO_INVALID_ARGUMENT,
        "Failed to set input buffer of {} due to mis-match in sizes. Got {}, Expected {}", 
        input.name(), input_buffer.size(), input.get_frame_size());

    m_input_memview = input_buffer;
    m_inference_manager->set_input_buffer(m_input_memview, input.name());
    return HAILO_SUCCESS;
}

hailo_status SingleIOModel::set_output_buffer(MemoryView output_buffer)
{
    CHECK(m_output_buffer == nullptr, HAILO_INVALID_OPERATION, "Failed to set input buffer, buffer was already allocated.");

    TRY(auto output, m_inference_manager->get_model()->output());
    CHECK(output_buffer.size() == output.get_frame_size(), HAILO_INVALID_ARGUMENT,
        "Failed to set output buffer of {} due to mis-match in sizes. Got {}, Expected {}", 
        output.name(), output_buffer.size(), output.get_frame_size());

    m_output_memview = output_buffer;
    m_inference_manager->set_output_buffer(m_output_memview, output.name());
    return HAILO_SUCCESS;
}

Expected<MemoryView> SingleIOModel::allocate_input_buffer()
{
    CHECK(m_input_buffer == nullptr, HAILO_INVALID_OPERATION, "Failed to allocate input buffer, buffer was already allocated.");
    TRY(auto input, m_inference_manager->get_model()->input());
    TRY(m_input_buffer, Buffer::create_shared(input.get_frame_size(), BufferStorageParams::create_dma()));

    m_input_memview = MemoryView(m_input_buffer);
    m_inference_manager->set_input_buffer(m_input_memview, input.name());
    return MemoryView(m_input_buffer);
}

Expected<MemoryView> SingleIOModel::allocate_output_buffer()
{
    CHECK(m_output_buffer == nullptr, HAILO_INVALID_OPERATION, "Failed to allocate output buffer, buffer was already allocated.");

    TRY(auto output, m_inference_manager->get_model()->output());
    TRY(m_output_buffer, Buffer::create_shared(output.get_frame_size(), BufferStorageParams::create_dma()));

    m_output_memview = MemoryView(m_output_buffer);
    m_inference_manager->set_output_buffer(m_output_memview, output.name());
    return MemoryView(m_output_buffer);
}

Expected<MemoryView> SingleIOModel::get_input_memview()
{
    assert(!m_input_memview.empty());
    return MemoryView(m_input_memview);
}

Expected<MemoryView> SingleIOModel::get_output_memview()
{
    assert(!m_output_memview.empty());
    return MemoryView(m_output_memview);
}

hailo_status SingleIOModel::execute()
{
    assert(!m_input_memview.empty());
    assert(!m_output_memview.empty());
    return m_inference_manager->generate();
}

Expected<size_t> SingleIOModel::get_input_frame_size()
{
    TRY(auto input, m_inference_manager->get_model()->input());
    return input.get_frame_size();
}

Expected<size_t> SingleIOModel::get_output_frame_size()
{
    TRY(auto output, m_inference_manager->get_model()->output());
    return output.get_frame_size();
}

std::shared_ptr<InferModel> SingleIOModel::get_model()
{
    return m_inference_manager->get_model();
}

} /* namespace genai */
} /* namespace hailort */
