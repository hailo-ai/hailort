/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolov8_post_process.hpp
 * @brief YOLOV8 post process
 *
 **/

#ifndef _HAILO_YOLOV8_POST_PROCESS_HPP_
#define _HAILO_YOLOV8_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops/op_metadata.hpp"
namespace hailort
{
namespace net_flow
{

struct Yolov8MatchingLayersNames
{
    // Regression layer
    std::string reg;

    // Classifications layer
    std::string cls;

    uint32_t stride;
};

struct Yolov8PostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector off two strings that represents the relations between the outputs names.
    std::vector<Yolov8MatchingLayersNames> reg_to_cls_inputs;
};

class Yolov8OpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const Yolov8PostProcessConfig &yolov8_post_process_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    Yolov8PostProcessConfig &yolov8_config() { return m_yolov8_config;};

private:
    Yolov8PostProcessConfig m_yolov8_config;
    Yolov8OpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const Yolov8PostProcessConfig &yolov8_post_process_config,
                       const std::string &network_name)
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOV8-Post-Process", network_name, OperationType::YOLOV8)
        , m_yolov8_config(yolov8_post_process_config)
    {}

    hailo_status validate_params() override;
};

class YOLOV8PostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov8OpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

private:
    std::shared_ptr<Yolov8OpMetadata> m_metadata;
    std::vector<float32_t> m_d_values_matrix;  // Holds the values of the bbox boundaries distances from the stride's center
    std::unordered_map<std::string, std::vector<std::vector<float32_t>>> m_d_matrix; // Holds the values from which we compute those distances
    YOLOV8PostProcessOp(std::shared_ptr<Yolov8OpMetadata> metadata)
        : NmsPostProcessOp(static_cast<std::shared_ptr<NmsOpMetadata>>(metadata))
        , m_metadata(metadata), m_d_values_matrix(NUM_OF_D_VALUES)
    {
        for (const auto &input_metadata : m_metadata->inputs_metadata()) {
            m_d_matrix[input_metadata.first] = std::vector<std::vector<float32_t>>(NUM_OF_D_VALUES,
                                                    std::vector<float32_t>(input_metadata.second.padded_shape.features / NUM_OF_D_VALUES));
        }
    }

    static const uint32_t CLASSES_START_INDEX = 0;
    static const uint32_t NO_OBJECTNESS = 1;
    static const uint32_t NUM_OF_D_VALUES = 4;

    template<typename DstType = float32_t, typename SrcType>
    hailo_status extract_detections(const Yolov8MatchingLayersNames &layers_names, const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        uint32_t stride)
    {
        const auto &inputs_metadata = m_metadata->inputs_metadata();
        const auto &nms_config = m_metadata->nms_config();

        assert(contains(inputs_metadata, layers_names.reg));
        assert(contains(inputs_metadata, layers_names.cls));
        const auto &reg_padded_shape = inputs_metadata.at(layers_names.reg).padded_shape;
        const auto &cls_padded_shape = inputs_metadata.at(layers_names.cls).padded_shape;
        const auto &reg_quant_info = inputs_metadata.at(layers_names.reg).quant_info;
        const auto &cls_quant_info = inputs_metadata.at(layers_names.cls).quant_info;

        // Validate regression buffer size
        auto number_of_entries = reg_padded_shape.height * reg_padded_shape.width;
        auto buffer_size = number_of_entries * reg_padded_shape.features * sizeof(SrcType);
        CHECK(buffer_size == reg_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, reg {} buffer_size should be {}, but is {}", layers_names.reg, buffer_size, reg_buffer.size());

        // Validate classes buffer size
        const uint32_t cls_entry_size = nms_config.number_of_classes;
        number_of_entries = cls_padded_shape.height * cls_padded_shape.width;
        buffer_size = number_of_entries * cls_entry_size * sizeof(SrcType);
        CHECK(buffer_size == cls_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, cls {} buffer_size should be {}, but is {}", layers_names.cls, buffer_size, cls_buffer.size());

        // Format is NHCW -> each row size is C size * W size
        auto cls_row_size = cls_padded_shape.features * cls_padded_shape.width;

        SrcType *reg_data = (SrcType*)reg_buffer.data();
        SrcType *cls_data = (SrcType*)cls_buffer.data();

        for (uint32_t row = 0; row < cls_padded_shape.height; row++) {
            for (uint32_t col = 0; col < cls_padded_shape.width; col++) {
                auto cls_idx = (cls_row_size * row) + col;

                if (nms_config.cross_classes) {
                    // Pre-NMS optimization. If NMS checks IoU over different classes, only the maximum class is relevant
                    auto max_id_score_pair = get_max_class<DstType, SrcType>(cls_data, cls_idx, CLASSES_START_INDEX,
                        NO_OBJECTNESS, cls_quant_info, cls_padded_shape.width);
                    if (max_id_score_pair.second >= nms_config.nms_score_th) {
                        // If passes threshold - get the relevant bbox and add this detection
                        assert(contains(m_d_matrix, layers_names.reg));
                        auto &d_matrix = m_d_matrix.at(layers_names.reg);
                        auto bbox = get_bbox<DstType, SrcType>(row, col, stride, reg_padded_shape, reg_quant_info,
                                                                (SrcType*)reg_data, d_matrix, max_id_score_pair.second);
                        m_detections.emplace_back(DetectionBbox(bbox, max_id_score_pair.first));
                        m_classes_detections_count[max_id_score_pair.first]++;
                    }
                }
                else {
                    // No optimization - it's possible that a specific bbox will hold more then 1 class
                    for (uint32_t curr_class_idx = 0; curr_class_idx < nms_config.number_of_classes; curr_class_idx++) {
                        auto class_entry_idx = cls_idx + (curr_class_idx * cls_padded_shape.width);
                        auto class_confidence = Quantization::dequantize_output<DstType, SrcType>(
                            cls_data[class_entry_idx], cls_quant_info);
                        if (class_confidence >= nms_config.nms_score_th) {
                            // If passes threshold - get the relevant bbox and add this detection
                            assert(contains(m_d_matrix, layers_names.reg));
                            auto &d_matrix = m_d_matrix.at(layers_names.reg);
                            auto bbox = get_bbox<DstType, SrcType>(row, col, stride, reg_padded_shape, reg_quant_info, 
                                                                    (SrcType*)reg_data, d_matrix, class_confidence);
                            m_detections.emplace_back(DetectionBbox(bbox, curr_class_idx));
                            m_classes_detections_count[curr_class_idx]++;
                        }
                    }
                }
            }
        }
        return HAILO_SUCCESS;
    }

    template<typename DstType = float32_t, typename SrcType>
    hailo_bbox_float32_t get_bbox(uint32_t row, uint32_t col, uint32_t stride, const hailo_3d_image_shape_t &reg_padded_shape,
        const hailo_quant_info_t &reg_quant_info, SrcType *reg_data, std::vector<std::vector<DstType>> &d_matrix, DstType class_confidence);

    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        uint32_t col, uint32_t row, uint32_t stride) const;

    static float32_t dot_product(std::vector<float> &values);

};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLOV8_POST_PROCESS_HPP_
