/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file graph_printer.cpp
 * @brief Implementation of graph_printer module
 **/

#include "graph_printer.hpp"
#include "hailo/hailort.h"
#include "common/filesystem.hpp"
#include "common/utils.hpp"
#include "infer_stats_printer.hpp"

#include <set>
#include <map>
#include <sstream>
#include <algorithm>

namespace hailort
{


PipelineGraphNode::PipelineGraphNode(DotWriter::RootGraph &G, std::shared_ptr<PipelineElement> elem,
                                     const std::vector<AccumulatorPtr> &runtime_stats_accumulators,
                                     DotWriter::Color::e node_color, DotWriter::Color::e node_bg_color,
                                     const std::string &node_style, DotWriter::LabelLoc::e node_label_loc,
                                     const std::string &sink_source_style, DotWriter::Color::e sink_source_color,
                                     DotWriter::NodeShape::e sink_source_shape, double sink_source_height,
                                     double sink_source_width, DotWriter::LabelLoc::e sub_label_loc) :
    m_graph_node(nullptr),
    m_sinks(),
    m_sources(),
    m_visited(false)
{
    assert(nullptr != elem);

    const auto label = create_multiline_label({ elem->name() /* TODO: other info will go here */ });
    const auto sub_label = format_runtime_stats(runtime_stats_accumulators);
    
    if (static_cast<std::string>(sub_label).empty()) {
        m_graph_node = G.AddCluster(label);
        assert(nullptr != m_graph_node);
    } else {
        auto *enclosing_cluster = G.AddCluster(sub_label);
        assert(nullptr != enclosing_cluster);

        enclosing_cluster->GetAttributes().SetLabelLoc(sub_label_loc);
        enclosing_cluster->GetAttributes().SetPeripheries(0);

        m_graph_node = enclosing_cluster->AddCluster(label);
        assert(nullptr != m_graph_node);
        m_graph_node->GetAttributes().SetTooltip(static_cast<std::string>(sub_label));
    }

    m_graph_node->GetAttributes().SetColor(node_color);
    m_graph_node->GetAttributes().SetBGColor(node_bg_color);
    m_graph_node->GetAttributes().SetStyle(node_style);
    m_graph_node->GetAttributes().SetLabelLoc(node_label_loc);
    m_graph_node->GetDefaultNodeAttributes().SetStyle(sink_source_style);
    m_graph_node->GetDefaultNodeAttributes().SetColor(sink_source_color);
    m_graph_node->GetDefaultNodeAttributes().SetShape(sink_source_shape);
    m_graph_node->GetDefaultNodeAttributes().SetHeight(sink_source_height);
    m_graph_node->GetDefaultNodeAttributes().SetWidth(sink_source_width);
    m_graph_node->GetDefaultNodeAttributes().SetFixedSize(true);

    for (const auto &sink : elem->sinks()) {
        add_sink(sink);
    }
    if (0 == elem->sinks().size()) {
        add_dummy_sink();
    }

    for (const auto &source : elem->sources()) {
        add_source(source);
    }
    if (0 == elem->sources().size()) {
        add_dummy_source();
    }
}


DotWriter::HtmlString PipelineGraphNode::create_multiline_label(const std::vector<std::string> &lines, Align align_to)
{
    if (lines.empty()) {
        // Note: A html <table> without any "<tr></tr>" tags is invalid, so if lines is empty, we return an empty string
        return DotWriter::HtmlString("");
    }

    std::stringstream result;
    result << "<table border=\"0\">" << std::endl;
    for (const auto& line : lines) {
        result << "\t<tr><td align=\"text\">" << line;
        if (align_to == Align::LEFT) {
            result << "<br align=\"left\" />";
        } else if (align_to == Align::RIGHT) {
            result << "<br align=\"right\" />";
        }
        result << "</td></tr>" << std::endl;
    }
    result << "</table>";
    return DotWriter::HtmlString(result.str());
}

DotWriter::HtmlString PipelineGraphNode::format_runtime_stats(const std::vector<AccumulatorPtr> &runtime_stats_accumulators)
{
    std::vector<std::string> lines;
    for (const auto &accumulator : runtime_stats_accumulators) {
        if (nullptr == accumulator) {
            continue;
        }

        const auto &accumulator_result = accumulator->get();
        if ((!accumulator_result.count()) || (accumulator_result.count().value() == 0)) {
            continue;
        }

        // We split the statistics into two lines
        std::stringstream string_stream;
        string_stream << "<B>" << accumulator->get_data_type() << ": </B>";
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.mean(), "mean") << ", ";
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.min(), "min") << ", ";
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.max(), "max") << ", ";
        lines.emplace_back(string_stream.str());

        // Clear the stream and format the next line
        string_stream.str("");
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.var(), "var") << ", ";
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.sd(), "sd") << ", ";
        string_stream << AccumulatorResultsHelper::format_statistic(accumulator_result.mean_sd(), "mean_sd");
        lines.emplace_back(string_stream.str());
    }

    return create_multiline_label(lines, Align::LEFT);
}

void PipelineGraphNode::add_sink(const hailort::PipelinePad &pad)
{
    auto *sink = m_graph_node->AddNode("sink");
    assert(nullptr != sink);
    sink->GetAttributes().SetTooltip(pad.name());

    m_sinks.emplace(pad.name(), sink);
}

void PipelineGraphNode::add_dummy_sink()
{
    auto *sink = m_graph_node->AddNode();
    assert(nullptr != sink);
    sink->GetAttributes().SetStyle("invis");

    m_sinks.emplace("dummy_sink", sink);
}

void PipelineGraphNode::add_source(const hailort::PipelinePad &pad)
{
    auto *source = m_graph_node->AddNode("source");
    assert(nullptr != source);
    source->GetAttributes().SetTooltip(pad.name());

    m_sources.emplace(pad.name(), source);
    
    if (!m_sinks.empty()) {
        const auto first_sink_name = m_sinks.cbegin()->first;
        auto *edge = m_graph_node->AddEdge(m_sinks[first_sink_name], source);
        assert(nullptr != edge);

        edge->GetAttributes().SetStyle("invis");
    }
}

void PipelineGraphNode::add_dummy_source()
{
    auto *source = m_graph_node->AddNode();
    assert(nullptr != source);
    source->GetAttributes().SetStyle("invis");

    m_sources.emplace("dummy_source", source);
    
    if (!m_sinks.empty()) {
        const auto first_sink_name = m_sinks.cbegin()->first;
        auto *edge = m_graph_node->AddEdge(m_sinks[first_sink_name], source);
        assert(nullptr != edge);

        edge->GetAttributes().SetStyle("invis");
    }
}

DotWriter::Node *PipelineGraphNode::get_pad(const std::string &pad_name) const
{
    if (m_sinks.count(pad_name) != 0) {
        return m_sinks.at(pad_name);
    }
    assert(m_sources.count(pad_name) != 0);
    return m_sources.at(pad_name);
}

bool PipelineGraphNode::has_been_visited() const
{
    return m_visited;
}

void PipelineGraphNode::set_visited()
{
    m_visited = true;
}

hailo_status GraphPrinter::write_dot_file(const std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>> &input_vstreams_per_network_group,
    const std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>> &output_vstreams_per_network_group, const std::string &graph_title,
    const std::string &output_path, bool write_pipeline_stats)
{
    PipelineGraph graph(input_vstreams_per_network_group, output_vstreams_per_network_group, graph_title, write_pipeline_stats);
    return graph.write_dot_file(output_path);
}


GraphPrinter::PipelineGraph::PipelineGraph(const std::vector<std::map<std::string, std::vector<std::reference_wrapper<InputVStream>>>> &input_vstreams_per_network_group,
                                           const std::vector<std::map<std::string, std::vector<std::reference_wrapper<OutputVStream>>>> &output_vstreams_per_network_group,
                                           const std::string &graph_title, bool write_pipeline_stats) :
    m_graph(true, create_graph_title_label(graph_title, DefaultNodeAttrs::MAIN_LABEL_FONT_SIZE)),
    m_elems_in_graph()
{
    // Set the graph "graph title" label to be on top
    m_graph.GetAttributes().SetLabelLoc(DotWriter::LabelLoc::T);
    // Set the graph direction from left to right
    m_graph.GetAttributes().SetRankDir(DotWriter::RankDir::LR);

    assert(input_vstreams_per_network_group.size() == output_vstreams_per_network_group.size());

    for (size_t network_group_index = 0; network_group_index < input_vstreams_per_network_group.size(); network_group_index++) {
        size_t total_inputs_count = 0;
        for (auto &input_vstreams_pair : input_vstreams_per_network_group[network_group_index]) {
            total_inputs_count += input_vstreams_pair.second.size();
        }
        size_t total_outputs_count = 0;
        for (auto &output_vstreams_pair : output_vstreams_per_network_group[network_group_index]) {
            total_outputs_count += output_vstreams_pair.second.size();
        }

        m_graph.GetAttributes().SetPackMode(format_pack_mode(total_outputs_count + total_inputs_count));

        // Note: This order is important (input pipelines will be printed above output pipelines)
        for (const auto &output_vstreams_pair : output_vstreams_per_network_group[network_group_index]) {
            for (const auto &vstream : output_vstreams_pair.second) {
                update_graph_nodes(vstream.get().get_pipeline(), write_pipeline_stats);
            }
        }
        for (const auto &input_vstreams_pair : input_vstreams_per_network_group[network_group_index]) {
            for (const auto &vstream : input_vstreams_pair.second) {
                update_graph_nodes(vstream.get().get_pipeline(), write_pipeline_stats);
            }
        }
        for (const auto &output_vstreams_pair : output_vstreams_per_network_group[network_group_index]) {
            for (const auto &vstream : output_vstreams_pair.second) {
                update_edges_in_graph(vstream.get().get_pipeline(), "HW", "user_output");
            }
        }
        for (const auto &input_vstreams_pair : input_vstreams_per_network_group[network_group_index]) {
            for (const auto &vstream : input_vstreams_pair.second) {
                update_edges_in_graph(vstream.get().get_pipeline(), "user_input", "HW");
            }
        }
    }
}
 
hailo_status GraphPrinter::PipelineGraph::write_dot_file(const std::string &output_path)
{
    CHECK(m_graph.WriteToFile(output_path), HAILO_FILE_OPERATION_FAILURE,
        "Faild writing graph '.dot' file to output_path='{}'", output_path);
    return HAILO_SUCCESS;
}

DotWriter::Node *GraphPrinter::PipelineGraph::add_external_node(const std::string &label,
    DotWriter::NodeShape::e shape, double height, double width)
{
    auto *result = m_graph.AddNode(label);
    assert(nullptr != result);

    result->GetAttributes().SetShape(shape);
    result->GetAttributes().SetHeight(height);
    result->GetAttributes().SetWidth(width);
    result->GetAttributes().SetFixedSize(true);
    return result;
}

void GraphPrinter::PipelineGraph::update_graph_nodes(const std::vector<std::shared_ptr<PipelineElement>> &pipeline,
    bool write_pipeline_stats)
{
    // We assume that PipelineElement names are unique (also across different vstreams)
    for (const auto& elem : pipeline) {
        const auto elem_name = elem->name();
        if (m_elems_in_graph.count(elem_name) != 0) {
            // This elem appears in the graph
            continue;
        }

        std::vector<AccumulatorPtr> runtime_stats_accumulators;
        if (write_pipeline_stats) {
            if (nullptr != elem->get_fps_accumulator()) {
                runtime_stats_accumulators.emplace_back(elem->get_fps_accumulator());
            }
            if (nullptr != elem->get_latency_accumulator()) {
                runtime_stats_accumulators.emplace_back(elem->get_latency_accumulator());
            }
            for (const auto &queue_size_accumulator : elem->get_queue_size_accumulators()) {
                if (nullptr != queue_size_accumulator) {
                    runtime_stats_accumulators.emplace_back(queue_size_accumulator);
                }
            }
        }
        PipelineGraphNode curr_elem_graph_node(m_graph, elem, runtime_stats_accumulators);
        m_elems_in_graph.emplace(elem_name, std::move(curr_elem_graph_node));
    }
}

void GraphPrinter::PipelineGraph::update_edges_in_graph_recursive(const PipelineElement &root,
    const std::string &output_elem_name)
{
    auto &curr_elem_graph_node = m_elems_in_graph.at(root.name());
    if (curr_elem_graph_node.has_been_visited()) {
        return;
    }
    curr_elem_graph_node.set_visited();

    for (const auto &source : root.sources()) {
        if (nullptr == source.next()) {
            auto *left = curr_elem_graph_node.get_pad(source.name());
            auto *right = add_external_node(output_elem_name);
            m_graph.AddEdge(left, right);
            continue;
        }
        const auto *sink = source.next();

        const auto &next_elem = sink->element();
        const auto next_elem_name = next_elem.name();

        auto *left = curr_elem_graph_node.get_pad(source.name());
        auto *right = m_elems_in_graph.at(next_elem_name).get_pad(sink->name());
        m_graph.AddEdge(left, right);

        update_edges_in_graph_recursive(next_elem, output_elem_name);
    }
}

void GraphPrinter::PipelineGraph::update_edges_in_graph(const std::vector<std::shared_ptr<PipelineElement>> &pipeline,
    const std::string &input_elem_name, const std::string &output_elem_name)  
{
    for (const auto &root_elem : get_pipeline_root_elements(pipeline)) {
        auto &root_elem_graph_node = m_elems_in_graph.at(root_elem->name());
        if (root_elem_graph_node.has_been_visited()) {
            continue;
        }

        for (const auto &sink : root_elem->sinks()) {
            auto *left = add_external_node(input_elem_name);
            auto *right = root_elem_graph_node.get_pad(sink.name());
            m_graph.AddEdge(left, right);
        }

        update_edges_in_graph_recursive(*root_elem, output_elem_name);
    }
}

std::string GraphPrinter::PipelineGraph::format_pack_mode(size_t num_rows)
{
    // Align the graph as an array of num_rows rows (see https://graphviz.org/docs/attr-types/packMode/)
    std::stringstream result;
    result << "array_t" << std::to_string(num_rows);
    return result.str();
}

DotWriter::HtmlString GraphPrinter::PipelineGraph::create_graph_title_label(const std::string &hef_path,
    uint32_t font_size)
{
    std::stringstream result;
    result << "<font point-size=\"" << std::to_string(font_size) << "\">";
    result << "<b>Network:</b> " << hef_path;
    result << "</font>";
    return DotWriter::HtmlString(result.str());
}

std::vector<std::shared_ptr<PipelineElement>> GraphPrinter::PipelineGraph::get_pipeline_root_elements(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline)
{
    std::vector<std::shared_ptr<PipelineElement>> root_elems;
    for (auto& elem : pipeline) {
        if ((0 == elem->sinks().size()) || std::all_of(elem->sinks().begin(), elem->sinks().end(),
                                                       [](auto &sink){ return nullptr == sink.prev(); })) {
            // A PipelineElement is a "root" if it has no sink pads or it's sink pads aren't connected to source pads
            root_elems.emplace_back(elem);
        }
    }

    return root_elems;
}

} /* namespace hailort */
