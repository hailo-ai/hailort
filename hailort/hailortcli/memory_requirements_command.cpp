/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file memory_requirements_command.cpp
 **/
#include "memory_requirements_command.hpp"
#include "common/filesystem.hpp"
#include "hef/memory_requirements_calculator.hpp"


using MemoryRequirementsNetworkParams = MemoryRequirementsCalculator::HefParams;

class MemoryRequirementsNetworkApp : public CLI::App {
public:
    MemoryRequirementsNetworkApp();
    MemoryRequirementsCalculator::HefParams get_params() const { return m_params; }

private:
    MemoryRequirementsCalculator::HefParams m_params;
};

class MemoryRequirementsApp : public CLI::App {
public:
    MemoryRequirementsApp();
    auto get_network_params() const { return m_network_params; }

private:
    void add_net_subcom();

    std::vector<MemoryRequirementsCalculator::HefParams> m_network_params;
};

MemoryRequirementsNetworkApp::MemoryRequirementsNetworkApp() :
    CLI::App("Set network", "set-net")
{
    auto hef_path_option = add_option("hef", m_params.hef_path, "HEF file path")->check(CLI::ExistingFile);
    add_option("--name", m_params.network_group_name, "Network group name")
        ->default_val("")
        ->needs(hef_path_option);
    add_option("--batch-size", m_params.batch_size, "Batch size")->default_val(HAILO_DEFAULT_BATCH_SIZE);
}

MemoryRequirementsApp::MemoryRequirementsApp() :
    CLI::App("Shows memory requirements for running models", "mem-req")
{
    add_net_subcom();
}

void MemoryRequirementsApp::add_net_subcom()
{
    auto net_app = std::make_shared<MemoryRequirementsNetworkApp>();
    net_app->immediate_callback();
    net_app->callback([this, net_app_weak=std::weak_ptr<MemoryRequirementsNetworkApp>(net_app)]() {
        auto net_app = net_app_weak.lock();
        if (!net_app) { return; }
        m_network_params.push_back(net_app->get_params());

        // Throw an error if anything is left over and should not be.
        _process_extras();

        remove_subcommand(net_app.get());
        // Remove from parsed_subcommands_ as well (probably a bug in CLI11)
        parsed_subcommands_.erase(std::remove_if(
            parsed_subcommands_.begin(), parsed_subcommands_.end(),
            [net_app](auto x){return x == net_app.get();}),
            parsed_subcommands_.end());
        add_net_subcom();
    });
    add_subcommand(net_app);
}

MemoryRequirementsCommand::MemoryRequirementsCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand(std::make_shared<MemoryRequirementsApp>()))
{}

static std::string pretty_byte_size_print(size_t size_bytes)
{
    auto size_bytes_d = static_cast<double>(size_bytes);
    auto ordered_sizes = {"B", "KB", "MB"};
    for (const auto &size_str : ordered_sizes) {
        if (size_bytes_d < 1024) {
            return fmt::format("{:.3} {}", size_bytes_d, size_str);
        }
        size_bytes_d /= 1024;
    }

    return fmt::format("{:.3} GB", size_bytes_d);
}

static std::string get_name(const MemoryRequirementsNetworkParams &net_params)
{
    auto name = Filesystem::basename(net_params.hef_path);
    if (!net_params.network_group_name.empty()) {
        name = fmt::format("{}:{}", name, net_params.network_group_name);
    }
    return name;
}

static std::string repeat(const std::string &str, size_t count)
{
    std::string result;
    for (size_t i = 0; i < count; i++) {
        result += str;
    }
    return result;
}

hailo_status MemoryRequirementsCommand::execute()
{
    auto &app = dynamic_cast<MemoryRequirementsApp&>(*m_app);
    CHECK(0 < app.get_network_params().size(), HAILO_INVALID_OPERATION, "Nothing to run");

    std::cout << "Parsing Hefs, calculating memory requirements...\n";
    const auto params = app.get_network_params();
    TRY(auto requirements, MemoryRequirementsCalculator::get_memory_requirements(params));
    std::cout << "Parsing Hefs, calculating memory requirements... DONE\n";

    const size_t model_name_size = 50;
    const size_t element_size = 10;
    const auto elements_count = 3; // CMA, CMA-Desc, Pinned

    const size_t per_type_size = element_size * elements_count + (elements_count - 1); // Size includes (elements_count - 1) delimiters
    const auto memory_types_count = 3; // Config, Intermediate, Total

    const auto header_seperator = "+" + repeat("-", model_name_size) + "+" +
        repeat(repeat("-", per_type_size) + "+", memory_types_count);
    const auto header_first_format = fmt::format("|{}|", repeat(" ", model_name_size)) +
        repeat(fmt::format("{{:^{}}}|", per_type_size), memory_types_count);
    const auto header_second_format = fmt::format("|{{:^{}}}|", model_name_size) +
        repeat(repeat("-", per_type_size) + "+", memory_types_count);

    const auto body_seperator = "+" + repeat("-", model_name_size) + "+" +
        repeat(repeat("-", element_size) + "+", elements_count * memory_types_count);
    const auto body_table_format = fmt::format("|{{:^{}}}|", model_name_size) +
        repeat(fmt::format("{{:^{}}}|", element_size), elements_count * memory_types_count);

    const auto print_row = [=](const std::string &name, const MemoryRequirements &req) {
        auto total = EdgeTypeMemoryRequirements{
            req.config_buffers.cma_memory + req.intermediate_buffers.cma_memory,
            req.config_buffers.cma_memory_for_descriptors + req.intermediate_buffers.cma_memory_for_descriptors,
            req.config_buffers.pinned_memory + req.intermediate_buffers.pinned_memory
        };
        std::cout << fmt::format(body_table_format, name,
            pretty_byte_size_print(req.config_buffers.cma_memory),
            pretty_byte_size_print(req.config_buffers.cma_memory_for_descriptors),
            pretty_byte_size_print(req.config_buffers.pinned_memory),
            pretty_byte_size_print(req.intermediate_buffers.cma_memory),
            pretty_byte_size_print(req.intermediate_buffers.cma_memory_for_descriptors),
            pretty_byte_size_print(req.intermediate_buffers.pinned_memory),
            pretty_byte_size_print(total.cma_memory),
            pretty_byte_size_print(total.cma_memory_for_descriptors),
            pretty_byte_size_print(total.pinned_memory)) << "\n";
    };

    std::cout << "Memory Requirements:\n";
    std::cout << header_seperator << "\n";
    std::cout << fmt::format(header_first_format, "Weights", "Inter", "Total") << "\n";
    std::cout << fmt::format(header_second_format, "Model") << "\n";

    std::cout << fmt::format(body_table_format, "", "CMA", "CMA-Desc", "Pinned",
        "CMA", "CMA-Desc", "Pinned", "CMA", "CMA-Desc", "Pinned")  << "\n";
    std::cout << body_seperator << "\n";
    for (size_t i = 0; i < params.size(); i++) {
        print_row(get_name(params[i]), requirements.hefs_memory_requirements[i]);
    }
    std::cout << body_seperator << "\n";
    print_row("Total", requirements.total_memory_requirements);
    std::cout << header_seperator << "\n";

    return HAILO_SUCCESS;
}
