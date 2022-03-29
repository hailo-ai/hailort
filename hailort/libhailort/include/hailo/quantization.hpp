/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file quantization.hpp
 * @brief Implemention of quantization functions.
 **/

#ifndef _HAILO_QUANTIZATION_HPP_
#define _HAILO_QUANTIZATION_HPP_

#include "hailo/hailort.h"
#include <math.h>
#include <fenv.h>

namespace hailort
{

class RoundingToNearestGuard final
{
public:
    RoundingToNearestGuard() :
        m_original_rounding_method(fegetround())
    {
        fesetround(FE_TONEAREST);
    }

    ~RoundingToNearestGuard()
    {
        fesetround(m_original_rounding_method);
    }

private:
    int m_original_rounding_method;
};

/*! Hailo device requires input data to be quantized/scaled before it is sent. Similarly, data outputted
 * from the device needs to be 'de-quantized'/rescaled as well.
 * When a neural network is compiled, each input/output layer in the neural network is assigned two floating point values
 * that are parameters to an input/output transformation:
 * ::hailo_quant_info_t::qp_zp (zero_point) and ::hailo_quant_info_t::qp_scale (fields of the struct ::hailo_quant_info_t).
 * These values are stored in the Hef.
 * - Input transformation: input data is divided by ::hailo_quant_info_t::qp_scale and
 *                         then ::hailo_quant_info_t::qp_zp is added to the result.
 * - Output transformation: ::hailo_quant_info_t::qp_zp is subtracted from output data and
 *                          then the result is multiplied by ::hailo_quant_info_t::qp_scale.
 **/
class Quantization final
{
public:
    Quantization() = delete;
    
    /**
     * De-quantize output buffer pointed by @a src_ptr from data type @a Q into the buffer pointed by @a dst_ptr of data type @a T.
     *
     * @param[in] src_ptr                   A pointer to the buffer containing the data that will be de-quantized.
     * @param[out] dst_ptr                  A pointer to the buffer that will contain the output de-quantized data.
     * @param[in] buffer_elements_count     The number of elements in @a src_ptr and @a dst_ptr arrays.
     * @param[in] quant_info                Quantization info.
     */
    template <typename T, typename Q>
    static void dequantize_output_buffer(Q *src_ptr, T *dst_ptr, uint32_t buffer_elements_count, hailo_quant_info_t quant_info)
    {
        if (is_identity_qp(quant_info)) {
            for (uint32_t i = 0; i < buffer_elements_count; i++) {
                dst_ptr[i] = (T)(src_ptr[i]);
            }
        } else {
            auto rounding_tonearest_guard = RoundingToNearestGuard();
            for (uint32_t i = 0; i < buffer_elements_count; i++) {
                dst_ptr[i] = dequantize_output<T, Q>(src_ptr[i], quant_info);
            }
        }
    }

    /**
     * De-quantize in place the output buffer pointed by @a dst_ptr from data type @a Q to data type @a T.
     * 
     * @param[inout] dst_ptr                A pointer to the buffer to be de-quantized.
     * @param[in] buffer_elements_count     The number of elements in @a dst_ptr array.
     * @param[in] quant_info                Quantization info.
     */
    template <typename T, typename Q>
    static void dequantize_output_buffer_in_place(T *dst_ptr, uint32_t buffer_elements_count, hailo_quant_info_t quant_info)
    {
        if (is_identity_qp(quant_info)) {
            for (int32_t i = (int32_t)buffer_elements_count - 1; i >= 0; i--) {
                dst_ptr[i] = (T)(*((Q*)dst_ptr + i));
            }
        } else {
            auto rounding_tonearest_guard = RoundingToNearestGuard();
            for (int32_t i = (int32_t)buffer_elements_count - 1; i >= 0; i--) {
                dst_ptr[i] = dequantize_output<T, Q>(*((Q*)dst_ptr + i), quant_info);
            }
        }
    }

    /**
     * Quantize input buffer pointed by @a src_ptr of data type @a T, into the buffer pointed by @a dst_ptr of data type @a Q.
     * 
     * @param[in] src_ptr                   A pointer to the buffer containing the data that will be quantized.
     * @param[out] dst_ptr                  A pointer to the buffer that will contain the output quantized data.
     * @param[in] buffer_elements_count     The number of elements in @a src_ptr and @a dst_ptr arrays.
     * @param[in] quant_info                Quantization info.
     */
    template <typename T, typename Q>
    static void quantize_input_buffer(T *src_ptr, Q *dst_ptr, uint32_t buffer_elements_count, hailo_quant_info_t quant_info)
    {
        auto rounding_tonearest_guard = RoundingToNearestGuard();
        if (is_identity_qp(quant_info)) {
            for (uint32_t i = 0; i < buffer_elements_count; i++) {
                dst_ptr[i] = (Q)rintf(src_ptr[i]);
            }
        } else {
            for (uint32_t i = 0; i < buffer_elements_count; i++) {
                dst_ptr[i] = quantize_input<T, Q>(src_ptr[i], quant_info);
            }
        }
    }

    /**
     * De-quantize output NMS buffer pointed by @a src_ptr of data type @a Q, into the buffer pointed by @a dst_ptr of data type @a T.
     * 
     * @param[in] src_ptr                   A pointer to the buffer containing the data that will be de-quantized.
     * @param[out] dst_ptr                  A pointer to the buffer that will contain the output de-quantized data.
     * @param[in] buffer_elements_count     The number of elements in @a src_ptr and @a dst_ptr arrays.
     * @param[in] quant_info                Quantization info.
     * @param[in] number_of_classes         Amount of NMS classes.
     */
    template <typename T, typename Q>
    static void dequantize_output_buffer_nms(Q *src_ptr, T *dst_ptr, uint32_t buffer_elements_count, hailo_quant_info_t quant_info, uint32_t number_of_classes)
    {
        auto rounding_tonearest_guard = RoundingToNearestGuard();
        (void)buffer_elements_count;
        size_t offset = 0;
        for (uint32_t i = 0; i < number_of_classes; i++) {
            size_t bbox_count = src_ptr[offset];
            dst_ptr[offset] = (T)(src_ptr[offset]);
            offset++;
            size_t class_end_offset = offset + (HailoRTCommon::BBOX_PARAMS * bbox_count);
            assert(class_end_offset <= buffer_elements_count);
            for (; offset < class_end_offset; offset++) {
                dst_ptr[offset] = dequantize_output<T, Q>(src_ptr[offset], quant_info);
            }
        }
    }

    static inline bool is_identity_qp(const hailo_quant_info_t &quant_info)
    {
        return ((1 == quant_info.qp_scale) && (0 == quant_info.qp_zp));
    }

private:
    template <typename T, typename Q>
    static inline Q quantize_input(T number, hailo_quant_info_t quant_info)
    {
        float32_t clipped_number = clip((float32_t)number, quant_info.limvals_min, quant_info.limvals_max);
        return (Q)rintf((clipped_number / quant_info.qp_scale) + quant_info.qp_zp);
    }

    static inline float32_t clip(float32_t n, float32_t limval_min, float32_t limval_max)
    {
        if (n >= limval_max) {
            return limval_max;
        }
        else if (n <= limval_min) {
            return limval_min;
        }
        else {
            return n;
        }
    }

    template <typename T, typename Q>
    static inline T dequantize_output(Q number, hailo_quant_info_t quant_info)
    {
        return (T)((number - quant_info.qp_zp) * quant_info.qp_scale);
    }
};

} /* namespace hailort */

#endif /* _HAILO_QUANTIZATION_HPP_ */
