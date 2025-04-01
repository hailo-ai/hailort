/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_utils.hpp
 * @brief Common buffer utility functions.
 **/

#ifndef _HAILO_HAILORTCLI_BUFFER_UTILS_HPP_
#define _HAILO_HAILORTCLI_BUFFER_UTILS_HPP_


#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"

#include <random>

using namespace hailort;

inline Expected<Buffer> create_uniformed_buffer(size_t size, BufferStorageParams params = BufferStorageParams::create_dma(), uint32_t seed = 0)
{
    auto buffer = Buffer::create(size, params);
    if (buffer) {
        // https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
        std::mt19937 gen(seed); // mersenne_twister_engine seeded with 'seed'
        std::uniform_int_distribution<> distrib(0, std::numeric_limits<uint8_t>::max());
        for (size_t i = 0; i < size; i++) {
            buffer->data()[i] = static_cast<uint8_t>(distrib(gen));
        }
    }
    return buffer;
}

inline Expected<std::shared_ptr<Buffer>> create_uniformed_buffer_shared(size_t size, BufferStorageParams params = BufferStorageParams::create_dma(), uint32_t seed = 0)
{
    auto buffer = create_uniformed_buffer(size, params, seed);
    if (buffer) {
        auto buffer_ptr = std::make_shared<Buffer>(buffer.release());
        if (nullptr == buffer_ptr) {
            return make_unexpected(HAILO_OUT_OF_HOST_MEMORY);
        }
        return buffer_ptr;
    }
    return make_unexpected(buffer.status());
}


#endif /* _HAILO_HAILORTCLI_BUFFER_UTILS_HPP_ */
