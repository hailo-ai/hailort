/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file graph_printer.hpp
 * @brief Print pipeline graphs to '.dot' files
 **/

#ifndef _HAILO_GRAPH_PRINTER_HPP_
#define _HAILO_GRAPH_PRINTER_HPP_

#include "hailo/hailort.h"
#include "hailo/vstream.hpp"

#include "net_flow/pipeline/pipeline.hpp"

#include "DotWriter.h"

#include <vector>
#include <memory>
#include <type_traits>


namespace hailort
{

class DefaultNodeAttrs final
{
public:
    DefaultNodeAttrs() = delete;

    // Titles
    static constexpr uint32_t MAIN_LABEL_FONT_SIZE = 28;
    static constexpr uint32_t PIPELINE_LABEL_FONT_SIZE = 24;
    
    // Pipeline nodes
    static constexpr DotWriter::Color::e PIPELINE_NODE_COLOR = DotWriter::Color::LIGHTGREY;
    static constexpr DotWriter::Color::e PIPELINE_NODE_BG_COLOR = DotWriter::Color::LIGHTGREY;
    static std::string PIPELINE_NODE_STYLE() { return "rounded"; };
    static constexpr DotWriter::LabelLoc::e PIPELINE_NODE_LABEL_LOC = DotWriter::LabelLoc::T;

    // Sink/source nodes
    static std::string SINK_SOURCE_STYLE() { return "filled"; };
    static constexpr DotWriter::Color::e SINK_SOURCE_COLOR = DotWriter::Color::WHITE;
    static constexpr DotWriter::NodeShape::e SINK_SOURCE_SHAPE = DotWriter::NodeShape::RECTANGLE;
    static constexpr double SINK_SOURCE_HEIGHT = 0.5;
    static constexpr double SINK_SOURCE_WIDTH = 0.7;
    
    // External nodes (HW, user_input, user_output)
    static constexpr DotWriter::NodeShape::e EXTERNAL_NODE_SHAPE = DotWriter::NodeShape::RECTANGLE;
    static constexpr double EXTERNAL_NODE_HEIGHT = 0.5;
    static constexpr double EXTERNAL_NODE_WIDTH = 1;

    // Sublabel
    static constexpr DotWriter::LabelLoc::e SUBLABEL_LOC = DotWriter::LabelLoc::B;
};

class PipelineGraphNode final
{
public:
    PipelineGraphNode(DotWriter::RootGraph &G, std::shared_ptr<PipelineElement> elem,
        const std::vector<AccumulatorPtr> &runtime_stats_accumulators,
        DotWriter::Color::e node_color = DefaultNodeAttrs::PIPELINE_NODE_COLOR,
        DotWriter::Color::e node_bg_color = DefaultNodeAttrs::PIPELINE_NODE_BG_COLOR,
        const std::string &node_style = DefaultNodeAttrs::PIPELINE_NODE_STYLE(),
        DotWriter::LabelLoc::e node_label_loc = DefaultNodeAttrs::PIPELINE_NODE_LABEL_LOC,
        const std::string &sink_source_style = DefaultNodeAttrs::SINK_SOURCE_STYLE(),
        DotWriter::Color::e sink_source_color = DefaultNodeAttrs::SINK_SOURCE_COLOR,
        DotWriter::NodeShape::e sink_source_shape = DefaultNodeAttrs::SINK_SOURCE_SHAPE,
        double sink_source_height = DefaultNodeAttrs::SINK_SOURCE_HEIGHT,
        double sink_source_width = DefaultNodeAttrs::SINK_SOURCE_WIDTH,
        DotWriter::LabelLoc::e sub_label_loc = DefaultNodeAttrs::SUBLABEL_LOC);

    PipelineGraphNode(PipelineGraphNode &&) = default;
    PipelineGraphNode(const PipelineGraphNode &) = delete;
    PipelineGraphNode &operator=(PipelineGraphNode &&) = delete;
    PipelineGraphNode &operator=(const PipelineGraphNode &) = delete;
    ~PipelineGraphNode() = default;

    DotWriter::Node *get_pad(const std::string &pad_name) const;
    bool has_been_visited() const;
    void set_visited();

private:
    enum class Align
    {
        LEFT,
        RIGHT,
        CENTER
    };

    // Creates a HtmlString with each line in lines on a new line, or an empty string if lines is empty
    static DotWriter::HtmlString create_multiline_label(const std::vector<std::string> &lines, Align align_to = Align::CENTER);
    static DotWriter::HtmlString format_runtime_stats(const std::vector<AccumulatorPtr> &runtime_stats_accumulators);

    // Note:
    // * Sink/source pads are nested under the pipeline element node.
    // * Sinks always appear on the left side of the pipeline element node, and sources on the right.
    // * If an element has no sinks/soruces, then an invisible "dummy" sink/source is added.
    //   This is used to align the other pads in the elem to the left/right (a Graphviz oddity).
    //   E.g:
    //   ------------------------------------               ------------------------------------
    //   |           Filter_Elem1           |               |            Sink_Elem1            |
    //   |                                  |               |                                  |
    //   |   --------           --------    |               |   --------                       |
    // --|-->| sink |           |source|----|---------------|-->| sink |       (dummy source)  |
    //   |   --------           --------    |               |   --------         (invisible)   |
    //   |                                  |               |                                  |
    //   ------------------------------------               ------------------------------------
    void add_sink(const hailort::PipelinePad &pad);
    void add_dummy_sink();
    void add_source(const hailort::PipelinePad &pad);
    void add_dummy_source();

    DotWriter::Cluster *m_graph_node;
    std::map<std::string, DotWriter::Node *> m_sinks;
    std::map<std::string, DotWriter::Node *> m_sources;
    bool m_visited;
};

class GraphPrinter
{
public:
    GraphPrinter() = delete;
    static hailo_status write_dot_file(const std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>> &input_vstreams_per_network_group,
        const std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>> &output_vstreams_per_network_group,
        const std::string &graph_title, const std::string &output_path, bool write_pipeline_stats);

private:
    class PipelineGraph final
    {
    public:
        PipelineGraph(const std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>> &input_vstreams_per_network_group,
            const std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>> &output_vstreams_per_network_group,
            const std::string &graph_title, bool write_pipeline_stats);
        hailo_status write_dot_file(const std::string &output_path);

    private:
        DotWriter::RootGraph m_graph;
        std::map<std::string, PipelineGraphNode> m_elems_in_graph;

        DotWriter::Node *add_external_node(const std::string &label,
            DotWriter::NodeShape::e shape = DefaultNodeAttrs::EXTERNAL_NODE_SHAPE,
            double height = DefaultNodeAttrs::EXTERNAL_NODE_HEIGHT,
            double width = DefaultNodeAttrs::EXTERNAL_NODE_WIDTH);
        // Add all the pipeline nodes in the graph to m_graph
        void update_graph_nodes(const std::vector<std::shared_ptr<PipelineElement>> &pipeline, bool write_pipeline_stats);
        // update_edges_* will be called after adding the graph nodes with 'update_graph_nodes'
        // Update the edges in the pipeline graph recursively, starting at root (using DFS) 
        void update_edges_in_graph_recursive(const PipelineElement &root, const std::string &output_elem_name);
        // Update all of the edges in the pipeline graph
        void update_edges_in_graph(const std::vector<std::shared_ptr<PipelineElement>> &pipeline,
            const std::string &input_elem_name, const std::string &output_elem_name);

        static std::string format_pack_mode(size_t num_rows);
        static DotWriter::HtmlString create_graph_title_label(const std::string &hef_path, uint32_t font_size);
        static std::vector<std::shared_ptr<PipelineElement>> get_pipeline_root_elements(
            const std::vector<std::shared_ptr<PipelineElement>> &pipeline);
    };
};

} /* namespace hailort */

#endif /* _HAILO_GRAPH_PRINTER_HPP_ */
