/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_driver.cpp
 * @brief Low level interface to PCI driver
 **/

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/driver/os/driver_os_specific.hpp"

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "common/utils.hpp"
#include "hailo_ioctl_common.h"

#if defined(__linux__)
#include <sys/mman.h>
#elif defined(__QNX__)
#include <fcntl.h>
#include <sys/mman.h>
#elif defined(_WIN32)
#pragma comment(lib, "cfgmgr32.lib")
#else
#error "unsupported platform!"
#endif

namespace hailort
{

static_assert(VDMA_CHANNELS_PER_ENGINE == MAX_VDMA_CHANNELS_PER_ENGINE, "Driver and libhailort parameters mismatch");
static_assert(MAX_VDMA_ENGINES == MAX_VDMA_ENGINES_COUNT, "Driver and libhailort parameters mismatch");
static_assert(MIN_D2H_CHANNEL_INDEX == VDMA_DEST_CHANNELS_START, "Driver and libhailort parameters mismatch");
static_assert(ONGOING_TRANSFERS_SIZE == HAILO_VDMA_MAX_ONGOING_TRANSFERS, "Driver and libhailort parameters mismatch");
static_assert(MAX_IRQ_TIMESTAMPS_SIZE == CHANNEL_IRQ_TIMESTAMPS_SIZE, "Driver and libhailort parameters mismatch");

static_assert(MAX_TRANSFER_BUFFERS_IN_REQUEST == HAILO_MAX_BUFFERS_PER_SINGLE_TRANSFER, "Driver and libhailort parameters mismatch");

static_assert(static_cast<int>(InterruptsDomain::NONE) == HAILO_VDMA_INTERRUPTS_DOMAIN_NONE, "Driver and libhailort parameters mismatch");
static_assert(static_cast<int>(InterruptsDomain::HOST) == HAILO_VDMA_INTERRUPTS_DOMAIN_HOST, "Driver and libhailort parameters mismatch");
static_assert(static_cast<int>(InterruptsDomain::DEVICE) == HAILO_VDMA_INTERRUPTS_DOMAIN_DEVICE, "Driver and libhailort parameters mismatch");
static_assert(static_cast<int>(InterruptsDomain::BOTH) ==
    (HAILO_VDMA_INTERRUPTS_DOMAIN_DEVICE | HAILO_VDMA_INTERRUPTS_DOMAIN_HOST), "Driver and libhailort parameters mismatch");


static const std::string INTEGRATED_NNC_DRIVER_PATH = "/dev/hailo_integrated_nnc";
static const std::string PCIE_EP_DRIVER_PATH = "/dev/hailo_pci_ep";


#define _RUN_IOCTL(ioctl_code, ioctl_name, params) [&]() { \
    LOGGER__DEBUG("Running ioctl {}", ioctl_name); \
    auto __err = run_ioctl((ioctl_code), (params)); \
    return (0 == __err) ? HAILO_SUCCESS : convert_errno_to_hailo_status(__err, ioctl_name); \
}()

#define RUN_IOCTL(ioctl, params) _RUN_IOCTL(ioctl, #ioctl, params)

#define RUN_AND_CHECK_IOCTL_RESULT(ioctl, params, message) \
    CHECK_SUCCESS(_RUN_IOCTL(ioctl, #ioctl, params), message)


static hailo_dma_buffer_type driver_dma_buffer_type_to_dma_buffer_type(HailoRTDriver::DmaBufferType buffer_type) {
    switch (buffer_type) {
    case HailoRTDriver::DmaBufferType::USER_PTR_BUFFER:
        return HAILO_DMA_USER_PTR_BUFFER;
    case HailoRTDriver::DmaBufferType::DMABUF_BUFFER:
        return HAILO_DMA_DMABUF_BUFFER;
    }

    assert(false);
    return HAILO_DMA_BUFFER_MAX_ENUM;
}

static enum hailo_cpu_id translate_cpu_id(hailo_cpu_id_t cpu_id)
{
    switch (cpu_id) {
    case HAILO_CPU_ID_0:
        return HAILO_CPU_ID_CPU0;
    case HAILO_CPU_ID_1:
        return HAILO_CPU_ID_CPU1;
    case HAILO_CPU_ID_MAX_ENUM:
        // Add label for HAILO_CPU_ID_MAX_ENUM to cover all enum cases (avoid warnings). Continue to the assert.
        break;
    }

    assert(false);
    // On release build Return value that will make ioctls to fail.
    return HAILO_CPU_ID_NONE;
}



static hailo_dma_data_direction direction_to_dma_data_direction(HailoRTDriver::DmaDirection direction) {
    switch (direction) {
    case HailoRTDriver::DmaDirection::H2D:
        return HAILO_DMA_TO_DEVICE;
    case HailoRTDriver::DmaDirection::D2H:
        return HAILO_DMA_FROM_DEVICE;
    case HailoRTDriver::DmaDirection::BOTH:
        return HAILO_DMA_BIDIRECTIONAL;
    }

    assert(false);
    // On release build Return value that will make ioctls to fail.
    return HAILO_DMA_NONE;
}

static HailoRTDriver::DeviceBoardType board_type_to_device_board_type(enum hailo_board_type board_type) {
    switch (board_type) {
    case HAILO_BOARD_TYPE_HAILO8:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO8;
    case HAILO_BOARD_TYPE_HAILO15:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO15;
    case HAILO_BOARD_TYPE_HAILO15L:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO15L;
    case HAILO_BOARD_TYPE_HAILO10H:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO10H;
    case HAILO_BOARD_TYPE_HAILO10H_LEGACY:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_HAILO10H_LEGACY;
    case HAILO_BOARD_TYPE_MARS:
        return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_MARS;
    default:
        LOGGER__ERROR("Invalid board type from ioctl {}", static_cast<int>(board_type));
        break;
    }

    assert(false);
    // On release build Return value that will make ioctls to fail.
    return HailoRTDriver::DeviceBoardType::DEVICE_BOARD_TYPE_COUNT;
}

// TODO: validate wraparounds for buffer/mapping handles in the driver (HRT-9509)
const uintptr_t HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const size_t HailoRTDriver::INVALID_DRIVER_VDMA_MAPPING_HANDLE_VALUE = INVALID_DRIVER_HANDLE_VALUE;
const uint8_t HailoRTDriver::INVALID_VDMA_CHANNEL_INDEX = INVALID_VDMA_CHANNEL;

#if defined(__linux__) || defined(_WIN32)
const vdma_mapped_buffer_driver_identifier HailoRTDriver::INVALID_MAPPED_BUFFER_DRIVER_IDENTIFIER = INVALID_DRIVER_HANDLE_VALUE;
#elif __QNX__
const vdma_mapped_buffer_driver_identifier HailoRTDriver::INVALID_MAPPED_BUFFER_DRIVER_IDENTIFIER = -1;
#else
#error "unsupported platform!"
#endif

Expected<std::unique_ptr<HailoRTDriver>> HailoRTDriver::create(const std::string &device_id, const std::string &dev_path)
{
    TRY(auto fd, open_device_file(dev_path));

    auto device_id_lower = StringUtils::to_lower(device_id);

    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<HailoRTDriver> driver(new (std::nothrow) HailoRTDriver(device_id_lower, std::move(fd), status));
    CHECK_NOT_NULL_AS_EXPECTED(driver, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return driver;
}

Expected<std::unique_ptr<HailoRTDriver>> HailoRTDriver::create_pcie(const std::string &device_id)
{
    TRY(const auto scan_results, scan_devices());

    auto device_found = std::find_if(scan_results.cbegin(), scan_results.cend(),
        [device_id=StringUtils::to_lower(device_id)](const auto &compared_scan_result) {
            return (device_id == compared_scan_result.device_id);
        });
    CHECK(device_found != scan_results.cend(), HAILO_INVALID_ARGUMENT, "Requested device not found");

    return create(device_found->device_id, device_found->dev_path);
}

Expected<std::unique_ptr<HailoRTDriver>> HailoRTDriver::create_integrated_nnc()
{
    return create(INTEGRATED_NNC_DEVICE_ID, INTEGRATED_NNC_DRIVER_PATH);
}

bool HailoRTDriver::is_integrated_nnc_loaded()
{
#if defined(_MSC_VER)
    // windows is not supported for integrated_nnc driver
    return false;
#else
    return (access(INTEGRATED_NNC_DRIVER_PATH.c_str(), F_OK) == 0);
#endif // defined(_MSC_VER)
}

Expected<std::unique_ptr<HailoRTDriver>> HailoRTDriver::create_pcie_ep()
{
    return create(PCIE_EP_DEVICE_ID, PCIE_EP_DRIVER_PATH);
}

bool HailoRTDriver::is_pcie_ep_loaded()
{
#if defined(_MSC_VER)
    // windows is not supported for pcie_ep driver
    return false;
#else
    return (access(PCIE_EP_DRIVER_PATH.c_str(), F_OK) == 0);
#endif // defined(_MSC_VER)
}

static hailo_status validate_driver_version(const hailo_driver_info &driver_info)
{
    hailo_version_t library_version{};
    auto status = hailo_get_library_version(&library_version);
    CHECK_SUCCESS(status);
    CHECK((driver_info.major_version == library_version.major) &&
        (driver_info.minor_version == library_version.minor) &&
        (driver_info.revision_version == library_version.revision), HAILO_INVALID_DRIVER_VERSION,
        "Driver version ({}.{}.{}) is different from library version ({}.{}.{})",
        driver_info.major_version, driver_info.minor_version, driver_info.revision_version,
        library_version.major, library_version.minor, library_version.revision);
    return HAILO_SUCCESS;
}

HailoRTDriver::HailoRTDriver(const std::string &device_id, FileDescriptor &&fd, hailo_status &status) :
    m_fd(std::move(fd)),
    m_device_id(device_id),
    m_allocate_driver_buffer(false)
{
    hailo_driver_info driver_info{};
    status = RUN_IOCTL(HAILO_QUERY_DRIVER_INFO, &driver_info);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to query driver info with {}", status);
        return;
    }
    status = validate_driver_version(driver_info);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Driver version mismatch, status {}", status);
        return;
    }

    hailo_device_properties device_properties{};
    status = RUN_IOCTL(HAILO_QUERY_DEVICE_PROPERTIES, &device_properties);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed query pcie device properties with {}", status);
        return;
    }

    m_desc_max_page_size = device_properties.desc_max_page_size;
    m_allocate_driver_buffer = (HAILO_ALLOCATION_MODE_DRIVER == device_properties.allocation_mode);
    m_dma_engines_count = device_properties.dma_engines_count;
    m_board_type = board_type_to_device_board_type(device_properties.board_type);
    if (DeviceBoardType::DEVICE_BOARD_TYPE_COUNT == m_board_type) {
        LOGGER__ERROR("Invalid board type returned from ioctl {}", static_cast<int>(device_properties.board_type));
        status = HAILO_DRIVER_INVALID_RESPONSE;
        return;
    }

    switch (device_properties.dma_type) {
    case HAILO_DMA_TYPE_PCIE:
        m_dma_type = DmaType::PCIE;
        break;
    case HAILO_DMA_TYPE_DRAM:
        m_dma_type = DmaType::DRAM;
        break;
    case HAILO_DMA_TYPE_PCI_EP:
        m_dma_type = DmaType::PCIE_EP;
        break;
    default:
        LOGGER__ERROR("Invalid dma type returned from ioctl {}", static_cast<int>(device_properties.dma_type));
        status = HAILO_DRIVER_INVALID_RESPONSE;
        return;
    }

    m_is_fw_loaded = device_properties.is_fw_loaded;

#ifdef __QNX__
    m_resource_manager_pid = device_properties.resource_manager_pid;
#endif // __QNX__

    status = HAILO_SUCCESS;
}

HailoRTDriver::~HailoRTDriver()
{
    for (const auto &buffer_info : m_mapped_buffer) {
        auto status = vdma_buffer_unmap_ioctl(buffer_info.second.handle);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to unmap buffer handle {} status {}", buffer_info.second.handle, status);
        }
    }
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> scan_all_devices()
{
    std::vector<HailoRTDriver::DeviceInfo> devices_info;

    TRY(auto nnc_devices, scan_nnc_devices());
    if (!nnc_devices.empty()) {
        devices_info.insert(devices_info.end(), nnc_devices.begin(), nnc_devices.end());
    }

    TRY(auto soc_devices, scan_soc_devices());
    if (!soc_devices.empty()) {
        devices_info.insert(devices_info.end(), soc_devices.begin(), soc_devices.end());
    }

    return devices_info;
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_devices()
{
    return scan_all_devices();
}

Expected<std::vector<HailoRTDriver::DeviceInfo>> HailoRTDriver::scan_devices(AcceleratorType acc_type)
{
    std::vector<HailoRTDriver::DeviceInfo> devices_info;

    if (AcceleratorType::SOC_ACCELERATOR == acc_type) {
        TRY(devices_info, scan_soc_devices());
    } else if (AcceleratorType::NNC_ACCELERATOR == acc_type) {
        TRY(devices_info, scan_nnc_devices());
    }

    return devices_info;
}

hailo_status HailoRTDriver::read_memory(MemoryType memory_type, uint64_t address, void *buf, size_t size)
{
    (void)memory_type;
    (void)address;
    (void)buf;
    (void)size;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status HailoRTDriver::write_memory(MemoryType memory_type, uint64_t address, const void *buf, size_t size)
{
    (void)memory_type;
    (void)address;
    (void)buf;
    (void)size;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status HailoRTDriver::vdma_enable_channels(const ChannelsBitmap &channels_bitmap, bool enable_timestamps_measure)
{
    CHECK(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_enable_channels_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);
    params.enable_timestamps_measure = enable_timestamps_measure;

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_ENABLE_CHANNELS, &params, "Failed to enable vdma channels");
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_disable_channels(const ChannelsBitmap &channels_bitmap)
{
    CHECK(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_disable_channels_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_DISABLE_CHANNELS, &params, "Failed to disable vdma channels");
    return HAILO_SUCCESS;
}

static Expected<ChannelInterruptTimestampList> create_interrupt_timestamp_list(
    hailo_vdma_interrupts_read_timestamp_params &inter_data)
{
    CHECK_AS_EXPECTED(inter_data.timestamps_count <= MAX_IRQ_TIMESTAMPS_SIZE, HAILO_DRIVER_INVALID_RESPONSE,
        "Invalid channel interrupts timestamps count returned {}", inter_data.timestamps_count);
    ChannelInterruptTimestampList timestamp_list{};

    timestamp_list.count = inter_data.timestamps_count;
    for (size_t i = 0; i < timestamp_list.count; i++) {
        timestamp_list.timestamp_list[i].timestamp = std::chrono::nanoseconds(inter_data.timestamps[i].timestamp_ns);
        timestamp_list.timestamp_list[i].desc_num_processed = inter_data.timestamps[i].desc_num_processed;
    }
    return timestamp_list;
}

static Expected<IrqData> to_irq_data(const hailo_vdma_interrupts_wait_params& params,
    uint8_t engines_count)
{
    static_assert(ARRAY_ENTRIES(IrqData::channels_irq_data) == ARRAY_ENTRIES(params.irq_data), "Mismatch irq data size");
    CHECK_AS_EXPECTED(params.channels_count <= ARRAY_ENTRIES(params.irq_data), HAILO_DRIVER_INVALID_RESPONSE,
        "Invalid channels count returned from vdma_interrupts_wait");

    IrqData irq{};
    irq.channels_count = params.channels_count;
    for (uint8_t i = 0; i < params.channels_count; i++) {
        const auto engine_index = params.irq_data[i].engine_index;
        const auto channel_index = params.irq_data[i].channel_index;
        CHECK_AS_EXPECTED(engine_index < engines_count, HAILO_DRIVER_INVALID_RESPONSE,
            "Invalid engine index {} returned from vdma_interrupts_wait, max {}", engine_index, engines_count);
        CHECK_AS_EXPECTED(channel_index < MAX_VDMA_CHANNELS_PER_ENGINE, HAILO_DRIVER_INVALID_RESPONSE,
            "Invalid channel_index index {} returned from vdma_interrupts_wait", channel_index);

        irq.channels_irq_data[i].channel_id.engine_index = engine_index;
        irq.channels_irq_data[i].channel_id.channel_index = channel_index;
        irq.channels_irq_data[i].validation_success = true;
        irq.channels_irq_data[i].is_active = true;

        if (params.irq_data[i].data == HAILO_VDMA_TRANSFER_DATA_CHANNEL_WITH_ERROR) {
            irq.channels_irq_data[i].validation_success = false;
        } else if (params.irq_data[i].data == HAILO_VDMA_TRANSFER_DATA_CHANNEL_NOT_ACTIVE) {
            irq.channels_irq_data[i].is_active = false;
        } else {
            irq.channels_irq_data[i].transfers_completed = params.irq_data[i].data;
        }
    }
    return irq;
}

Expected<IrqData> HailoRTDriver::vdma_interrupts_wait(const ChannelsBitmap &channels_bitmap)
{
    CHECK_AS_EXPECTED(is_valid_channels_bitmap(channels_bitmap), HAILO_INVALID_ARGUMENT, "Invalid channel bitmap given");
    hailo_vdma_interrupts_wait_params params{};
    std::copy(channels_bitmap.begin(), channels_bitmap.end(), params.channels_bitmap_per_engine);

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_INTERRUPTS_WAIT, &params, "Failed wait vdma interrupts");

    return to_irq_data(params, static_cast<uint8_t>(m_dma_engines_count));
}

Expected<ChannelInterruptTimestampList> HailoRTDriver::vdma_interrupts_read_timestamps(vdma::ChannelId channel_id)
{
    hailo_vdma_interrupts_read_timestamp_params params{};
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_INTERRUPTS_READ_TIMESTAMPS, &params, "Failed read vdma interrupts timestamps");

    return create_interrupt_timestamp_list(params);
}

Expected<std::vector<uint8_t>> HailoRTDriver::read_notification()
{
    hailo_d2h_notification notification_buffer{};
    auto status = RUN_IOCTL(HAILO_READ_NOTIFICATION, &notification_buffer);
    if (HAILO_SUCCESS != status) {
        LOGGER__DEBUG("Failed read notification, {}", status);
        return make_unexpected(status);
    }

    std::vector<uint8_t> notification(notification_buffer.buffer_len);
    memcpy(notification.data(), notification_buffer.buffer, notification_buffer.buffer_len);
    return notification;
}

hailo_status HailoRTDriver::disable_notifications()
{
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_DISABLE_NOTIFICATION, nullptr, "Failed disable notifications");
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::fw_control(const void *request, size_t request_len, const uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH],
    void *response, size_t *response_len, uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH],
    std::chrono::milliseconds timeout, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(request);
    CHECK_ARG_NOT_NULL(response);
    CHECK_ARG_NOT_NULL(response_len);
    CHECK(timeout.count() >= 0, HAILO_INVALID_ARGUMENT);

    hailo_fw_control command{};
    static_assert(PCIE_EXPECTED_MD5_LENGTH == sizeof(command.expected_md5), "mismatch md5 size");
    memcpy(&command.expected_md5, request_md5, sizeof(command.expected_md5));
    command.buffer_len = static_cast<uint32_t>(request_len);
    CHECK(request_len <= sizeof(command.buffer), HAILO_INVALID_ARGUMENT,
        "FW control request len can't be larger than {} (size given {})", sizeof(command.buffer), request_len);
    memcpy(&command.buffer, request, request_len);
    command.timeout_ms = static_cast<uint32_t>(timeout.count());
    command.cpu_id = translate_cpu_id(cpu_id);

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_FW_CONTROL, &command, "Failed in fw_control");

    if (*response_len < command.buffer_len) {
        LOGGER__ERROR("FW control response len needs to be at least {} (size given {})", command.buffer_len, *response_len);
        *response_len = command.buffer_len;
        return HAILO_INSUFFICIENT_BUFFER;
    }
    memcpy(response, command.buffer, command.buffer_len);
    *response_len = command.buffer_len;
    memcpy(response_md5, command.expected_md5, PCIE_EXPECTED_MD5_LENGTH);

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::read_log(uint8_t *buffer, size_t buffer_size, size_t *read_bytes, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(read_bytes);

    hailo_read_log_params params{};
    params.cpu_id = translate_cpu_id(cpu_id);
    params.buffer_size = buffer_size;
    params.read_bytes = 0;

    CHECK(buffer_size <= sizeof(params.buffer), HAILO_DRIVER_INVALID_RESPONSE,
        "Given buffer size {} is bigger than buffer size used to read logs {}", buffer_size, sizeof(params.buffer));

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_READ_LOG, &params, "Failed to read fw log");

    CHECK(params.read_bytes <= sizeof(params.buffer), HAILO_DRIVER_INVALID_RESPONSE,
        "Amount of bytes read from log {} is bigger than size of buffer {}", params.read_bytes, sizeof(params.buffer));

    memcpy(buffer, params.buffer, params.read_bytes);
    *read_bytes = params.read_bytes;

    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::reset_nn_core()
{
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_RESET_NN_CORE, nullptr, "Failed reset nn_core");
    return HAILO_SUCCESS;
}

Expected<uint64_t> HailoRTDriver::write_action_list(uint8_t *data, size_t size)
{
    hailo_write_action_list_params params{};
    params.size = size;
    params.data = data;

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_WRITE_ACTION_LIST, &params, "Failed write action list");

    uint64_t dma_address = params.dma_address;
    return dma_address;
}

Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map_dmabuf(int dmabuf_fd, size_t required_size,
    DmaDirection data_direction, DmaBufferType buffer_type)
{
    CHECK_AS_EXPECTED (DmaBufferType::DMABUF_BUFFER == buffer_type, HAILO_INVALID_ARGUMENT,
        "Error, Invalid buffer type given, buffer type {}", static_cast<int>(buffer_type));

    return vdma_buffer_map(static_cast<uintptr_t>(dmabuf_fd), required_size, data_direction,
        INVALID_MAPPED_BUFFER_DRIVER_IDENTIFIER, buffer_type);
}

Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map(uintptr_t user_address, size_t required_size,
    DmaDirection data_direction, const vdma_mapped_buffer_driver_identifier &driver_buff_handle,
    DmaBufferType buffer_type) {

    std::unique_lock<std::mutex> mapping_lock(m_mapped_buffer_lock);
    auto mapped_buffer_key = MappedBufferKey{user_address, data_direction, required_size};
    auto mapped_buffer = m_mapped_buffer.find(mapped_buffer_key);

    if (mapped_buffer != m_mapped_buffer.end()) {
        // Buffer already mapped, increase ref count and use it.
        assert(mapped_buffer->second.mapped_count > 0);
        const bool mismatched_driver_handle = (driver_buff_handle != INVALID_MAPPED_BUFFER_DRIVER_IDENTIFIER) &&
            (mapped_buffer->second.driver_buff_handle != driver_buff_handle);
        CHECK(!mismatched_driver_handle, HAILO_INVALID_ARGUMENT,
            "Mapped buffer driver handle 0x{:x} is different than required handle 0x{:x}", mapped_buffer->second.driver_buff_handle,
            driver_buff_handle);

        mapped_buffer->second.mapped_count++;
        return Expected<VdmaBufferHandle>(mapped_buffer->second.handle);
    } else {
        // Buffer not mapped, map it now
        auto handle = vdma_buffer_map_ioctl(user_address, required_size, data_direction,
            driver_buff_handle, buffer_type);
        CHECK_EXPECTED(handle);

        const auto mapping_count = 1;
        m_mapped_buffer[mapped_buffer_key] = MappedBufferInfo {
            handle.value(),
            driver_buff_handle,
            mapping_count
        };

        return handle.release();
    }
}

hailo_status HailoRTDriver::vdma_buffer_unmap(uintptr_t user_address, size_t size, DmaDirection data_direction)
{
    std::unique_lock<std::mutex> mapping_lock(m_mapped_buffer_lock);
    auto mapped_buffer_key = MappedBufferKey{user_address, data_direction, size};
    auto mapped_buffer = m_mapped_buffer.find(mapped_buffer_key);
    CHECK(mapped_buffer != m_mapped_buffer.end(), HAILO_NOT_FOUND, "Mapped buffer {} {} not found",
        user_address, size);

    assert(mapped_buffer->second.mapped_count > 0);
    mapped_buffer->second.mapped_count--;
    if (mapped_buffer->second.mapped_count == 0) {
        const auto handle = mapped_buffer->second.handle;
        m_mapped_buffer.erase(mapped_buffer_key);
        return vdma_buffer_unmap_ioctl(handle);
    }
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::vdma_buffer_sync(VdmaBufferHandle handle, DmaSyncDirection sync_direction,
    size_t offset, size_t count)
{
#ifndef __QNX__
    hailo_vdma_buffer_sync_params sync_info{};
    sync_info.handle = handle;
    sync_info.sync_type = (sync_direction == DmaSyncDirection::TO_HOST) ? HAILO_SYNC_FOR_CPU : HAILO_SYNC_FOR_DEVICE;
    sync_info.offset = offset;
    sync_info.count = count;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_BUFFER_SYNC, &sync_info, "Failed sync vdma buffer");
    return HAILO_SUCCESS;
// TODO: HRT-6717 - Remove ifdef when Implement sync ioctl (if determined needed in qnx)
#else /*  __QNX__ */
    (void) handle;
    (void) sync_direction;
    (void) offset;
    (void) count;
    return HAILO_SUCCESS;
#endif
}

hailo_status HailoRTDriver::descriptors_list_program(uintptr_t desc_handle, VdmaBufferHandle buffer_handle,
    size_t buffer_size, size_t buffer_offset, uint8_t channel_index, uint32_t starting_desc, uint32_t batch_size,
    bool should_bind, InterruptsDomain last_desc_interrupts, uint32_t stride)
{
    hailo_desc_list_program_params params{};
    params.buffer_handle = buffer_handle;
    params.buffer_size = buffer_size;
    params.buffer_offset = buffer_offset;
    params.batch_size = batch_size;
    params.desc_handle = desc_handle;
    params.channel_index = channel_index;
    params.starting_desc = starting_desc;

    params.should_bind = should_bind;
    params.last_interrupts_domain = (hailo_vdma_interrupts_domain)last_desc_interrupts;
    params.stride = stride;

#ifdef NDEBUG
    params.is_debug = false;
#else
    params.is_debug = true;
#endif

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_DESC_LIST_PROGRAM, &params, "Failed bind buffer to desc list");
    return HAILO_SUCCESS;
}

hailo_status HailoRTDriver::launch_transfer(vdma::ChannelId channel_id, uintptr_t desc_handle,
    uint32_t starting_desc, const std::vector<TransferBuffer> &transfer_buffers,
    bool should_bind, InterruptsDomain first_desc_interrupts, InterruptsDomain last_desc_interrupts)
{
    CHECK(is_valid_channel_id(channel_id), HAILO_INVALID_ARGUMENT, "Invalid channel id {} given", channel_id);
    CHECK(transfer_buffers.size() <= ARRAY_ENTRIES(hailo_vdma_launch_transfer_params::buffers), HAILO_INVALID_ARGUMENT,
        "Invalid transfer buffers size {} given", transfer_buffers.size());

    hailo_vdma_launch_transfer_params params{};
    params.engine_index = channel_id.engine_index;
    params.channel_index = channel_id.channel_index;
    params.desc_handle = desc_handle;
    params.starting_desc = starting_desc;
    params.buffers_count = static_cast<uint8_t>(transfer_buffers.size());
    for (size_t i = 0; i < transfer_buffers.size(); i++) {
        params.buffers[i].buffer_type = transfer_buffers[i].is_dma_buf ? 
            HAILO_DMA_DMABUF_BUFFER : HAILO_DMA_USER_PTR_BUFFER;
        params.buffers[i].addr_or_fd = transfer_buffers[i].addr_or_fd;
        params.buffers[i].size = static_cast<uint32_t>(transfer_buffers[i].size);
    }
    params.should_bind = should_bind;
    params.first_interrupts_domain = (hailo_vdma_interrupts_domain)first_desc_interrupts;
    params.last_interrupts_domain = (hailo_vdma_interrupts_domain)last_desc_interrupts;

#ifdef NDEBUG
    params.is_debug = false;
#else
    params.is_debug = true;
#endif

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_LAUNCH_TRANSFER, &params, "Failed launch transfer");

    return HAILO_SUCCESS;
}

#if defined(__linux__)
Expected<uintptr_t> HailoRTDriver::vdma_low_memory_buffer_alloc(size_t size)
{
    hailo_allocate_low_memory_buffer_params params{};
    params.buffer_size = size;
    params.buffer_handle = 0;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_LOW_MEMORY_BUFFER_ALLOC, &params, "Failed to allocate buffer");

    return std::move(params.buffer_handle);
}

hailo_status HailoRTDriver::vdma_low_memory_buffer_free(uintptr_t buffer_handle)
{
    hailo_free_low_memory_buffer_params params{};
    params.buffer_handle = buffer_handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_LOW_MEMORY_BUFFER_FREE, &params, "Failed to free allocated buffer");
    return HAILO_SUCCESS;
}

Expected<ContinousBufferInfo> HailoRTDriver::vdma_continuous_buffer_alloc(size_t size)
{
    auto handle_to_dma_address_pair = continous_buffer_alloc_ioctl(size);
    if (!handle_to_dma_address_pair) {
        // Log in continous_buffer_alloc_ioctl
        return make_unexpected(handle_to_dma_address_pair.status());
    }

    const auto desc_handle = handle_to_dma_address_pair->first;
    const auto dma_address = handle_to_dma_address_pair->second;

    auto user_address = continous_buffer_mmap(desc_handle, size);
    if (!user_address) {
        auto status = continous_buffer_free_ioctl(desc_handle);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed releasing conitnous buffer, status {}", status);
            // continue
        }
        return make_unexpected(user_address.status());
    }

    return ContinousBufferInfo{desc_handle, dma_address, size, user_address.release()};
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(const ContinousBufferInfo &buffer_info)
{
    hailo_status status = HAILO_SUCCESS;

    auto unmap_status = continous_buffer_munmap(buffer_info.user_address, buffer_info.size);
    if (HAILO_SUCCESS != unmap_status) {
        LOGGER__ERROR("Continous buffer list unmap failed with {}", unmap_status);
        status = unmap_status;
        // continue
    }

    auto release_status = continous_buffer_free_ioctl(buffer_info.handle);
    if (HAILO_SUCCESS != release_status) {
        LOGGER__ERROR("Continous buffer release status failed with {}", release_status);
        status = release_status;
        // continue
    }

    return status;
}
#elif defined(__QNX__) || defined(_WIN32)

Expected<uintptr_t> HailoRTDriver::vdma_low_memory_buffer_alloc(size_t /* size */)
{
    LOGGER__ERROR("Low memory buffer not supported for platform");
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

hailo_status HailoRTDriver::vdma_low_memory_buffer_free(uintptr_t /* buffer_handle */)
{
    LOGGER__ERROR("Low memory buffer not supported for platform");
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<ContinousBufferInfo> HailoRTDriver::vdma_continuous_buffer_alloc(size_t /* size */)
{
    LOGGER__ERROR("Continous buffer not supported for platform");
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

hailo_status HailoRTDriver::vdma_continuous_buffer_free(const ContinousBufferInfo &/* buffer_info */)
{
    LOGGER__ERROR("Continous buffer not supported for platform");
    return HAILO_NOT_SUPPORTED;
}

#else
#error "unsupported platform!"
#endif

hailo_status HailoRTDriver::mark_as_used()
{
    hailo_mark_as_in_use_params params{};
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_MARK_AS_IN_USE, &params, "Failed mark as used");
    return params.in_use ? HAILO_DEVICE_IN_USE : HAILO_SUCCESS;
}

Expected<std::pair<vdma::ChannelId, vdma::ChannelId>> HailoRTDriver::soc_connect(uint16_t port_number,
    uintptr_t input_buffer_desc_handle, uintptr_t output_buffer_desc_handle)
{
    hailo_soc_connect_params params{};
    params.port_number = port_number;
    params.input_desc_handle = input_buffer_desc_handle;
    params.output_desc_handle = output_buffer_desc_handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_SOC_CONNECT, &params, "Failed soc_connect");
    vdma::ChannelId input_channel{0, params.input_channel_index};
    vdma::ChannelId output_channel{0, params.output_channel_index};
    return std::make_pair(input_channel, output_channel);
}

Expected<std::pair<vdma::ChannelId, vdma::ChannelId>> HailoRTDriver::pci_ep_accept(uint16_t port_number,
    uintptr_t input_buffer_desc_handle, uintptr_t output_buffer_desc_handle)
{
    hailo_pci_ep_accept_params params{};
    params.port_number = port_number;
    params.input_desc_handle = input_buffer_desc_handle;
    params.output_desc_handle = output_buffer_desc_handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_PCI_EP_ACCEPT, &params, "Failed pci_ep accept");
    vdma::ChannelId input_channel{0, params.input_channel_index};
    vdma::ChannelId output_channel{0, params.output_channel_index};
    return std::make_pair(input_channel, output_channel);
}

hailo_status HailoRTDriver::close_connection(vdma::ChannelId input_channel, vdma::ChannelId output_channel,
    PcieSessionType session_type)
{
    if (PcieSessionType::SERVER == session_type) {
        hailo_pci_ep_close_params params{};
        params.input_channel_index = input_channel.channel_index;
        params.output_channel_index = output_channel.channel_index;
        RUN_AND_CHECK_IOCTL_RESULT(HAILO_PCI_EP_CLOSE, &params, "Failed pci_ep_close");
        return HAILO_SUCCESS;
    } else if (PcieSessionType::CLIENT == session_type) {
        hailo_soc_close_params params{};
        params.input_channel_index = input_channel.channel_index;
        params.output_channel_index = output_channel.channel_index;
        RUN_AND_CHECK_IOCTL_RESULT(HAILO_SOC_CLOSE, &params, "Failed soc_close");
        return HAILO_SUCCESS;
    } else {
        LOGGER__ERROR("close_connection not supported with session type {}", static_cast<int>(session_type));
        return HAILO_NOT_SUPPORTED;
    }
}

#if defined(__linux__)
static bool is_blocking_ioctl(unsigned long request)
{
    switch (request) {
    case HAILO_VDMA_INTERRUPTS_WAIT:
    case HAILO_FW_CONTROL:
    case HAILO_READ_NOTIFICATION:
    case HAILO_PCI_EP_ACCEPT:
        return true;
    default:
        return false;
    }
}

template<typename PointerType>
int HailoRTDriver::run_ioctl(uint32_t ioctl_code, PointerType param)
{
    // We lock m_driver lock on all request but the blocking onces. Read m_driver_lock doc in the header
    std::unique_lock<std::mutex> lock;
    if (!is_blocking_ioctl(ioctl_code)) {
        lock = std::unique_lock<std::mutex>(m_driver_lock);
    }

    return run_hailo_ioctl(m_fd, ioctl_code, param);
}
#elif defined(__QNX__) || defined(_WIN32)

template<typename PointerType>
int HailoRTDriver::run_ioctl(uint32_t ioctl_code, PointerType param)
{
    return run_hailo_ioctl(m_fd, ioctl_code, param);
}
#else
#error "Unsupported platform"
#endif





#if defined(__linux__) || defined(_WIN32)
Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map_ioctl(uintptr_t user_address, size_t required_size,
    DmaDirection data_direction, const vdma_mapped_buffer_driver_identifier &driver_buff_handle,
    DmaBufferType buffer_type)
{
    hailo_vdma_buffer_map_params map_user_buffer_info{};
    map_user_buffer_info.user_address = user_address;
    map_user_buffer_info.size = required_size;
    map_user_buffer_info.data_direction = direction_to_dma_data_direction(data_direction);
    map_user_buffer_info.buffer_type = driver_dma_buffer_type_to_dma_buffer_type(buffer_type);
    map_user_buffer_info.allocated_buffer_handle = driver_buff_handle;
    map_user_buffer_info.mapped_handle = 0;

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info, "Failed map vdma buffer, please make sure using compatible api(dma buffer or raw buffer)");

    return std::move(map_user_buffer_info.mapped_handle);
}
#elif defined(__QNX__)
Expected<HailoRTDriver::VdmaBufferHandle> HailoRTDriver::vdma_buffer_map_ioctl(uintptr_t user_address, size_t required_size,
    DmaDirection data_direction, const vdma_mapped_buffer_driver_identifier &driver_buff_handle,
    DmaBufferType buffer_type)
{
    // Mapping is done by the driver_buff_handle (shm file descriptor), and not by address.
    (void)user_address;
    CHECK(driver_buff_handle != INVALID_MAPPED_BUFFER_DRIVER_IDENTIFIER, HAILO_NOT_SUPPORTED,
        "On QNX only shared-memory buffers are allowed to be mapped");

    // Create shared memory handle to send to driver
    shm_handle_t shm_handle;
    int err = shm_create_handle(driver_buff_handle, m_resource_manager_pid, O_RDWR,
        &shm_handle, 0);
    if (0 != err) {
        LOGGER__ERROR("Error creating shm object handle, errno is: {}", errno);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    hailo_vdma_buffer_map_params map_user_buffer_info {
        .shared_memory_handle = shm_handle,
        .size = required_size,
        .data_direction = direction_to_dma_data_direction(data_direction),
        .buffer_type = driver_dma_buffer_type_to_dma_buffer_type(buffer_type),
        .allocated_buffer_handle = INVALID_DRIVER_HANDLE_VALUE,
        .mapped_handle = 0
    };

    // Note: The driver will accept the shm_handle, and will mmap it to its own address space. After the driver maps the
    // the shm, calling shm_delete_handle is not needed (but can't harm on the otherhand).
    // If the ioctl fails, we can't tell if the shm was mapped or not, so we delete it ourself.
    auto status = RUN_IOCTL(HAILO_VDMA_BUFFER_MAP, &map_user_buffer_info);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to map user buffer with {}", status);
        shm_delete_handle(shm_handle);
        return make_unexpected(status);
    }

    return VdmaBufferHandle(map_user_buffer_info.mapped_handle);
}
#else
#error "unsupported platform!"
#endif // __linux__

hailo_status HailoRTDriver::vdma_buffer_unmap_ioctl(VdmaBufferHandle handle)
{
    hailo_vdma_buffer_unmap_params unmap_user_buffer_info{};
    unmap_user_buffer_info.mapped_handle = handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_BUFFER_UNMAP, &unmap_user_buffer_info, "Failed unmap vdma buffer");
    return HAILO_SUCCESS;
}

Expected<DescriptorsListInfo> HailoRTDriver::descriptors_list_create(size_t desc_count,
    uint16_t desc_page_size, bool is_circular)
{
    CHECK(is_powerof2(desc_page_size), HAILO_INVALID_ARGUMENT, "Invalid desc page size {}", desc_page_size);

    hailo_desc_list_create_params create_desc_info{};
    create_desc_info.desc_count = desc_count;
    create_desc_info.desc_page_size = desc_page_size;
    create_desc_info.is_circular = is_circular;

    RUN_AND_CHECK_IOCTL_RESULT(HAILO_DESC_LIST_CREATE, &create_desc_info, "Failed to create desc list");

    return DescriptorsListInfo{create_desc_info.desc_handle, create_desc_info.dma_address};
}

hailo_status HailoRTDriver::descriptors_list_release(const DescriptorsListInfo &desc_info)
{
    struct hailo_desc_list_release_params params{};
    params.desc_handle = desc_info.handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_DESC_LIST_RELEASE, &params, "Failed release desc list");
    return HAILO_SUCCESS;
}

#if defined(__linux__)

Expected<std::pair<uintptr_t, uint64_t>> HailoRTDriver::continous_buffer_alloc_ioctl(size_t size)
{
    hailo_allocate_continuous_buffer_params params{};
    params.buffer_size = size;
    params.buffer_handle = 0;
    params.dma_address = 0;

    auto status = RUN_IOCTL(HAILO_VDMA_CONTINUOUS_BUFFER_ALLOC, &params);
    if (HAILO_OUT_OF_HOST_CMA_MEMORY == status) {
        LOGGER__INFO("Out of CMA memory for continous buffer, size {}", size);
        return make_unexpected(status);
    } else {
        CHECK_SUCCESS(status, "Failed to allocate continous buffer, size {}", size);
    }

    return std::make_pair(params.buffer_handle, params.dma_address);
}

hailo_status HailoRTDriver::continous_buffer_free_ioctl(uintptr_t buffer_handle)
{
    hailo_free_continuous_buffer_params params{};
    params.buffer_handle = buffer_handle;
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_VDMA_CONTINUOUS_BUFFER_FREE, &params, "Failed free continuous buffer");
    return HAILO_SUCCESS;
}

Expected<void *> HailoRTDriver::continous_buffer_mmap(uintptr_t desc_handle, size_t size)
{
    // We lock m_driver_lock before calling mmap. Read m_driver_lock doc in the header
    std::unique_lock<std::mutex> lock(m_driver_lock);

    void *address = mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_SHARED, m_fd, (off_t)desc_handle);
    if (MAP_FAILED == address) {
        LOGGER__ERROR("Failed to continous buffer buffer with errno: {}", errno);
        return make_unexpected(HAILO_DRIVER_OPERATION_FAILED);
    }
    return address;
}

hailo_status HailoRTDriver::continous_buffer_munmap(void *address, size_t size)
{
    if (0 != munmap(address, size)) {
        LOGGER__ERROR("munmap of address {}, length: {} failed with errno: {}", address, size, errno);
        return HAILO_DRIVER_OPERATION_FAILED;
    }
    return HAILO_SUCCESS;
}

#endif

bool HailoRTDriver::is_valid_channel_id(const vdma::ChannelId &channel_id)
{
    return (channel_id.engine_index < m_dma_engines_count) && (channel_id.channel_index < MAX_VDMA_CHANNELS_PER_ENGINE);
}

hailo_status HailoRTDriver::reset_chip()
{
    RUN_AND_CHECK_IOCTL_RESULT(HAILO_SOC_POWER_OFF, nullptr, "Failed poweroff");
    return HAILO_SUCCESS;
}
} /* namespace hailort */
