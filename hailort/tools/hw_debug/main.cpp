/**
 * @file main.cpp
 * @brief Main function, and shell build for the tool.
 */

#include "shell.hpp"
#include "readline_wrapper.hpp"
#include "memory_commands.hpp"
#include "driver_memory.hpp"

#include "CLI/CLI.hpp"

#include <array>

using namespace hailort;

static constexpr const char *LOGO = 
    R"( _____       _           _     __   __  )" "\n"
    R"(|  __ \     | |         | |    \ \ / /  )" "\n" 
    R"(| |  | | ___| |__   __ _| | ___ \ V /   )" "\n"
    R"(| |  | |/ _ \ '_ \ / _` | |/ _ \ > <    )" "\n"
    R"(| |__| |  __/ |_) | (_| | |  __// . \   )" "\n"
    R"(|_____/ \___|_.__/ \__,_|_|\___/_/ \_\  )" "\n";


static std::shared_ptr<Shell> add_memory_subshell(Shell &base_shell,
    const std::string &name, const std::string &short_name, std::shared_ptr<MemorySource> mem)
{
    auto subshell = base_shell.add_subshell(name, short_name);
    subshell->add_command(std::make_unique<MemoryReadCommand<uint32_t>>(mem));
    subshell->add_command(std::make_unique<MemoryReadCommand<uint16_t>>(mem));
    subshell->add_command(std::make_unique<MemoryReadCommand<uint8_t>>(mem));

    subshell->add_command(std::make_unique<MemoryWriteCommand<uint32_t>>(mem));
    subshell->add_command(std::make_unique<MemoryWriteCommand<uint16_t>>(mem));
    subshell->add_command(std::make_unique<MemoryWriteCommand<uint8_t>>(mem));

    if (!mem->get_fields().empty()) {
        subshell->add_command(std::make_unique<PrintCommand>(mem));
    }

    return subshell;
}

template<typename DriverMemorySourceType = DriverMemorySource>
static std::shared_ptr<Shell> add_driver_memory_subshell(Shell &base_shell,
    const std::string &name, const std::string &short_name,
    std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type)
{
    auto mem = std::make_shared<DriverMemorySourceType>(driver, memory_type);
    return add_memory_subshell(base_shell, name, short_name, mem);
}

static std::unique_ptr<Shell> create_pcie_accelerator_shell(std::shared_ptr<HailoRTDriver> driver_ptr)
{
    auto shell = std::make_unique<Shell>("> ");
    add_driver_memory_subshell<VdmaMemorySource>(*shell, "vdma", "v", driver_ptr, MemoryType::VDMA0);
    add_driver_memory_subshell(*shell, "bar0", "b0", driver_ptr, MemoryType::PCIE_BAR0);
    add_driver_memory_subshell(*shell, "bar2", "b2", driver_ptr, MemoryType::PCIE_BAR2);
    add_driver_memory_subshell(*shell, "bar4", "b4", driver_ptr, MemoryType::PCIE_BAR4);
    add_driver_memory_subshell(*shell, "mem", "m", driver_ptr, MemoryType::DIRECT_MEMORY);
    return shell;
}

static std::unique_ptr<Shell> create_vpu_shell(std::shared_ptr<HailoRTDriver> driver_ptr)
{
    auto shell = std::make_unique<Shell>("> ");
    add_driver_memory_subshell<VdmaMemorySource>(*shell, "vdma0", "v0", driver_ptr, MemoryType::VDMA0);
    add_driver_memory_subshell<VdmaMemorySource>(*shell, "vdma1", "v1", driver_ptr, MemoryType::VDMA1);
    add_driver_memory_subshell<VdmaMemorySource>(*shell, "vdma2", "v2", driver_ptr, MemoryType::VDMA2);
    add_driver_memory_subshell<DramDmaEngineMemorySource>(*shell, "engine0", "e0", driver_ptr, MemoryType::DMA_ENGINE0);
    add_driver_memory_subshell<DramDmaEngineMemorySource>(*shell, "engine1", "e1", driver_ptr, MemoryType::DMA_ENGINE1);
    add_driver_memory_subshell<DramDmaEngineMemorySource>(*shell, "engine2", "e2", driver_ptr, MemoryType::DMA_ENGINE2);
    add_driver_memory_subshell(*shell, "mem", "m", driver_ptr, MemoryType::DIRECT_MEMORY);
    return shell;
}

static std::vector<std::string> get_available_device_ids()
{
    auto scan_results = HailoRTDriver::scan_devices();
    if (!scan_results) {
        throw std::runtime_error("Failed scan pci");
    }
    if (scan_results->empty()) {
        throw std::runtime_error("No hailo devices on the system...");
    }

    std::vector<std::string> device_ids;
    for (const auto &scan_result : scan_results.value()) {
        device_ids.push_back(scan_result.device_id);
    }
    return device_ids;
}

HailoRTDriver::DeviceInfo get_device_info(const std::string &device_id)
{
    auto scan_results = HailoRTDriver::scan_devices();
    if (!scan_results) {
        throw std::runtime_error("Failed scan pci");
    }

    auto device_found = std::find_if(scan_results->cbegin(), scan_results->cend(),
        [&device_id](const auto &compared_scan_result) {
            return device_id == compared_scan_result.device_id;
        });
    if (device_found == std::end(scan_results.value())) {
        throw std::runtime_error("Requested device not found");
    }

    return *device_found;
}

std::shared_ptr<HailoRTDriver> create_driver_object(const std::string &device_id)
{
    auto device_info = get_device_info(device_id);
    auto hailort_driver = HailoRTDriver::create(device_info);
    if (!hailort_driver) {
        throw std::runtime_error("Failed create hailort driver object");
    }
    return hailort_driver.release();
}

int main(int argc, char **argv)
{
    try {
        ReadLineWrapper::init_library();

        auto available_device_ids = get_available_device_ids();

        CLI::App app{"Debalex"};
        std::string device_id = available_device_ids[0];
        app.add_option("-s,--device-id", device_id, "Device id")
            ->check(CLI::IsMember(available_device_ids));
        CLI11_PARSE(app, argc, argv);

        auto driver = create_driver_object(device_id);


        auto shell =
            driver->dma_type() == HailoRTDriver::DmaType::PCIE ?
                create_pcie_accelerator_shell(driver) :
                create_vpu_shell(driver);

        std::cout << LOGO << std::endl;
        shell->run_forever();
        return 0;
    }
    catch (const std::exception &exc) {
        std::cerr << "Failure: " << exc.what();
        return 1;
    }
}
