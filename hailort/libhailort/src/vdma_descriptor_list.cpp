#include "vdma_descriptor_list.hpp"

#define DESC_STATUS_REQ                       (1 << 0)
#define DESC_STATUS_REQ_ERR                   (1 << 1)
#define DESC_REQUREST_IRQ_PROCESSED           (1 << 2)
#define DESC_REQUREST_IRQ_ERR                 (1 << 3)

#define PCIE_DMA_HOST_INTERRUPTS_BITMASK      (1 << 5)
#define PCIE_DMA_DEVICE_INTERRUPTS_BITMASK    (1 << 4)

#define DRAM_DMA_HOST_INTERRUPTS_BITMASK      (1 << 4)
#define DRAM_DMA_DEVICE_INTERRUPTS_BITMASK    (1 << 5)

#define DESC_PAGE_SIZE_SHIFT                  (8)
#define DESC_PAGE_SIZE_MASK                   (0xFFFFFF00)

namespace hailort
{

Expected<VdmaDescriptorList> VdmaDescriptorList::create(uint32_t desc_count, uint16_t requested_desc_page_size,
    HailoRTDriver &driver)
{
    hailo_status status = HAILO_UNINITIALIZED;
    auto desc_page_size_value = driver.calc_desc_page_size(requested_desc_page_size);
    VdmaDescriptorList object(desc_count, driver, desc_page_size_value, status);
    if (HAILO_SUCCESS != status) {
        return make_unexpected(status);
    }

    return object;
}

VdmaDescriptorList::VdmaDescriptorList(uint32_t desc_count, HailoRTDriver &driver, uint16_t desc_page_size,
                                       hailo_status &status) :
    m_mapped_list(),
    m_count(desc_count),
    m_depth(0),
    m_desc_handle(0),
    m_dma_address(0),
    m_driver(driver),
    m_desc_page_size(desc_page_size)
{
    if (!is_powerof2(desc_count)) {
        LOGGER__ERROR("Descriptor count ({}) must be power of 2", desc_count);
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto depth = calculate_desc_list_depth(desc_count);
    if (!depth) {
        status = depth.status();
        return;
    }
    m_depth = depth.value();

    auto desc_handle_phys_addr_pair = m_driver.descriptors_list_create(desc_count);
    if (!desc_handle_phys_addr_pair) {
        status = desc_handle_phys_addr_pair.status();
        return;
    }

    m_desc_handle = desc_handle_phys_addr_pair->first;
    m_dma_address = desc_handle_phys_addr_pair->second;
    
    auto mapped_list = MmapBuffer<VdmaDescriptor>::create_file_map(desc_count * sizeof(VdmaDescriptor), m_driver.fd(), m_desc_handle);
    if (!mapped_list) {
        LOGGER__ERROR("Failed to memory map descriptors. desc handle: {:X}", m_desc_handle);
        status = mapped_list.status();
        return;
    }

    m_mapped_list = mapped_list.release();
    status = HAILO_SUCCESS;
}

VdmaDescriptorList::~VdmaDescriptorList()
{
    if (HAILO_SUCCESS != m_mapped_list.unmap()) {
        LOGGER__ERROR("Failed to release descriptors mapping");
    }

    // Note: The descriptors_list is freed by the desc_handle (no need to use the phys_address to free)
    if (0 != m_desc_handle) {
        if(HAILO_SUCCESS != m_driver.descriptors_list_release(m_desc_handle)) {
            LOGGER__ERROR("Failed to release descriptor list {}", m_desc_handle);
        }
    }
}

VdmaDescriptorList::VdmaDescriptorList(VdmaDescriptorList &&other) noexcept : 
    m_mapped_list(std::move(other.m_mapped_list)),
    m_count(std::move(other.m_count)),
    m_depth(std::move(other.m_depth)),
    m_desc_handle(std::exchange(other.m_desc_handle, 0)),
    m_dma_address(std::exchange(other.m_dma_address, 0)),
    m_driver(other.m_driver),
    m_desc_page_size(other.m_desc_page_size)  {}

Expected<uint8_t> VdmaDescriptorList::calculate_desc_list_depth(size_t count)
{
    // Calculate log2 of m_count (by finding the offset of the MSB)
    uint32_t depth = 0;
    while (count >>= 1) {
        ++depth;
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(depth), HAILO_INTERNAL_FAILURE, "Calculated desc_list_depth is too big: {}", depth);
    return static_cast<uint8_t>(depth);
}

hailo_status VdmaDescriptorList::configure_to_use_buffer(vdma::MappedBuffer& buffer, uint8_t channel_index)
{
    return m_driver.descriptors_list_bind_vdma_buffer(m_desc_handle, buffer.handle(), m_desc_page_size,
        channel_index);
}

hailo_status VdmaDescriptorList::configure_to_use_buffer(vdma::MappedBuffer& buffer)
{
    return configure_to_use_buffer(buffer, HailoRTDriver::INVALID_VDMA_CHANNEL_INDEX);
}

Expected<uint16_t> VdmaDescriptorList::program_descriptors(size_t transfer_size,
    VdmaInterruptsDomain first_desc_interrupts_domain, VdmaInterruptsDomain last_desc_interrupts_domain,
    size_t desc_offset, bool is_circular)
{
    const auto required_descriptors = descriptors_in_buffer(transfer_size);
    // Required_descriptors + desc_offset can't reach m_count. We need to keep at least 1 free desc at all time.
    if ((!is_circular) && ((required_descriptors + desc_offset) >= m_count)){
        LOGGER__ERROR("Requested transfer size ({}) result in more descrptors than available ({})", transfer_size, m_count);
        return make_unexpected(HAILO_OUT_OF_DESCRIPTORS);
    }

    size_t desc_index = desc_offset;
    for (size_t i = 0; i < required_descriptors - 1; ++i) {
        const auto interrupts_domain = (i == 0) ? first_desc_interrupts_domain : VdmaInterruptsDomain::NONE;
        program_single_descriptor((*this)[desc_index], m_desc_page_size, interrupts_domain);
        desc_index = (desc_index + 1) & (m_count - 1);
    }

    /* write residue page with the remaining buffer size*/
    auto resuide = transfer_size - (required_descriptors - 1) * m_desc_page_size;
    assert(IS_FIT_IN_UINT16(resuide));
    program_single_descriptor((*this)[desc_index], static_cast<uint16_t>(resuide), last_desc_interrupts_domain);

    return std::move(static_cast<uint16_t>(required_descriptors));
}

Expected<uint16_t> VdmaDescriptorList::program_descs_for_ddr_transfers(uint32_t row_size, bool should_raise_interrupt,
    uint32_t number_of_rows_per_intrpt, uint32_t buffered_rows, uint16_t initial_descs_offset, bool is_circular)
{
    uint16_t programmed_descs = 0;
    size_t offset = initial_descs_offset;
    assert(0 == (buffered_rows % number_of_rows_per_intrpt));

    auto first_desc_interrupts_mask = VdmaInterruptsDomain::NONE;
    auto last_desc_interrupts_mask = (should_raise_interrupt) ? VdmaInterruptsDomain::HOST : VdmaInterruptsDomain::NONE;
    for (uint32_t rows_count = 0; rows_count < buffered_rows; rows_count += number_of_rows_per_intrpt) {
        auto desc_count_local = program_descriptors((row_size * number_of_rows_per_intrpt),
            first_desc_interrupts_mask, last_desc_interrupts_mask, offset, is_circular);
        CHECK_EXPECTED(desc_count_local);
        offset = (offset + desc_count_local.value()) & (m_count - 1);
        programmed_descs = static_cast<uint16_t>(programmed_descs + desc_count_local.value());
    }
    return programmed_descs;
}

uint32_t VdmaDescriptorList::descriptors_in_buffer(size_t buffer_size) const
{
    return descriptors_in_buffer(buffer_size, m_desc_page_size);
}

uint32_t VdmaDescriptorList::descriptors_in_buffer(size_t buffer_size, uint16_t desc_page_size)
{
    assert(buffer_size < std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(((buffer_size) + desc_page_size - 1) / desc_page_size);
}

uint32_t VdmaDescriptorList::calculate_descriptors_count(uint32_t buffer_size, uint16_t batch_size, uint16_t desc_page_size)
{
    // Because we use cyclic buffer, the amount of active descs is lower by one that the amount
    // of descs given  (Otherwise we won't be able to determine if the buffer is empty or full).
    // Therefore we add 1 in order to compensate.
    uint32_t descs_count = std::min(((descriptors_in_buffer(buffer_size, desc_page_size) * batch_size) + 1),
        MAX_DESCS_COUNT);

    return get_nearest_powerof_2(descs_count, MIN_DESCS_COUNT);
}

Expected<std::pair<uint16_t, uint32_t>> VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer(
    const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size)
{
    // Note: If the pages pointed to by the descriptors are copied in their entirety, then DEFAULT_DESC_PAGE_SIZE
    //       is the optimal value. For transfer_sizes smaller than DEFAULT_DESC_PAGE_SIZE using smaller descriptor page
    //       sizes will save memory consuption without harming performance. In the case of nms for example, only one bbox
    //       is copied from each page. Hence, we'll use MIN_DESC_PAGE_SIZE for nms.
    const uint32_t initial_desc_page_size = (DEFAULT_DESC_PAGE_SIZE > transfer_size) ? 
        get_nearest_powerof_2(transfer_size, MIN_DESC_PAGE_SIZE) : DEFAULT_DESC_PAGE_SIZE;
    if (DEFAULT_DESC_PAGE_SIZE != initial_desc_page_size) {
        LOGGER__INFO("Using non-default initial_desc_page_size of {}, due to a small transfer size ({})",
            initial_desc_page_size, transfer_size);
    }
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(initial_desc_page_size), HAILO_INTERNAL_FAILURE,
        "Descriptor page size needs to fit in 16B");
    
    return get_desc_buffer_sizes_for_single_transfer_impl(driver, min_batch_size, max_batch_size, transfer_size,
        static_cast<uint16_t>(initial_desc_page_size));
}

Expected<std::pair<uint16_t, uint32_t>> VdmaDescriptorList::get_desc_buffer_sizes_for_multiple_transfers(
    const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes)
{
    return get_desc_buffer_sizes_for_multiple_transfers_impl(driver, batch_size, transfer_sizes,
        DEFAULT_DESC_PAGE_SIZE);
}

Expected<std::pair<uint16_t, uint32_t>> VdmaDescriptorList::get_desc_buffer_sizes_for_single_transfer_impl(
    const HailoRTDriver &driver, uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size,
    uint16_t initial_desc_page_size)
{
    auto results =  VdmaDescriptorList::get_desc_buffer_sizes_for_multiple_transfers_impl(driver, min_batch_size,
        {transfer_size}, initial_desc_page_size);
    CHECK_EXPECTED(results);

    auto page_size = results->first;

    auto desc_count = std::min(MAX_DESCS_COUNT,
            VdmaDescriptorList::calculate_descriptors_count(transfer_size, max_batch_size, page_size));

    return std::make_pair(page_size, desc_count);
}

Expected<std::pair<uint16_t, uint32_t>> VdmaDescriptorList::get_desc_buffer_sizes_for_multiple_transfers_impl(
    const HailoRTDriver &driver, uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes,
    uint16_t initial_desc_page_size)
{
    const uint16_t min_desc_page_size = driver.calc_desc_page_size(MIN_DESC_PAGE_SIZE);
    const uint16_t max_desc_page_size = driver.calc_desc_page_size(MAX_DESC_PAGE_SIZE);
    // Defined as uint32_t to prevent overflow (as we multiply it by two in each iteration of the while loop bellow)
    uint32_t local_desc_page_size = driver.calc_desc_page_size(initial_desc_page_size);
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size), HAILO_INTERNAL_FAILURE,
        "Descriptor page size needs to fit in 16B");
    CHECK_AS_EXPECTED(local_desc_page_size <= max_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is larger than maximum descriptor page size ({})",
        local_desc_page_size, max_desc_page_size);
    CHECK_AS_EXPECTED(local_desc_page_size >= min_desc_page_size, HAILO_INTERNAL_FAILURE,
        "Initial descriptor page size ({}) is smaller than minimum descriptor page size ({})",
        local_desc_page_size, min_desc_page_size);

    uint32_t acc_desc_count = 0;
    for (const auto &transfer_size : transfer_sizes) {
        acc_desc_count +=
            VdmaDescriptorList::descriptors_in_buffer(transfer_size, static_cast<uint16_t>(local_desc_page_size));
    }

    // Too many descriptors; try a larger desc_page_size which will lead to less descriptors used
    while ((acc_desc_count * batch_size) > (MAX_DESCS_COUNT - 1)) {
        local_desc_page_size <<= 1;

        CHECK_AS_EXPECTED(local_desc_page_size <= max_desc_page_size, HAILO_OUT_OF_DESCRIPTORS,
            "Network shapes and batch size exceeds driver descriptors capabilities. "
            "Required descriptors count: {}, max allowed on the driver: {}.",
            (batch_size * acc_desc_count), MAX_DESCS_COUNT);

        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(local_desc_page_size), HAILO_INTERNAL_FAILURE,
            "Descriptor page size needs to fit in 16B");

        acc_desc_count = 0;
        for (auto &transfer_size : transfer_sizes) {
            acc_desc_count +=
                VdmaDescriptorList::descriptors_in_buffer(transfer_size, static_cast<uint16_t>(local_desc_page_size));
        }
    }

    // Found desc_page_size and acc_desc_count
    const auto desc_page_size = static_cast<uint16_t>(local_desc_page_size);

    // Find descs_count
    const auto descs_count = get_nearest_powerof_2(acc_desc_count, MIN_DESCS_COUNT);
    CHECK_AS_EXPECTED(descs_count <= MAX_DESCS_COUNT, HAILO_OUT_OF_DESCRIPTORS);

    if (initial_desc_page_size != desc_page_size) {
        LOGGER__WARNING("Desc page size value ({}) is not optimal for performance.", desc_page_size);
    }

    return std::make_pair(desc_page_size, descs_count);
}

uint32_t VdmaDescriptorList::get_interrupts_bitmask(VdmaInterruptsDomain interrupts_domain)
{
    uint32_t host_bitmask = 0;
    uint32_t device_bitmask = 0;

    switch (m_driver.dma_type()) {
    case HailoRTDriver::DmaType::PCIE:
        host_bitmask = PCIE_DMA_HOST_INTERRUPTS_BITMASK;
        device_bitmask = PCIE_DMA_DEVICE_INTERRUPTS_BITMASK;
        break;
    case HailoRTDriver::DmaType::DRAM:
        host_bitmask = DRAM_DMA_HOST_INTERRUPTS_BITMASK;
        device_bitmask = DRAM_DMA_DEVICE_INTERRUPTS_BITMASK;
        break;
    default:
        assert(true);
    }

    uint32_t bitmask = 0;
    if (host_interuptes_enabled(interrupts_domain)) {
        bitmask |= host_bitmask;
    }
    if (device_interuptes_enabled(interrupts_domain)) {
        bitmask |= device_bitmask;
    }

    return bitmask;
}

void VdmaDescriptorList::program_single_descriptor(VdmaDescriptor &descriptor, uint16_t page_size,
    VdmaInterruptsDomain interrupts_domain)
{
    descriptor.PageSize_DescControl = 0;
    // Update the descriptor's PAGE_SIZE field in the control register with the maximum size of the DMA page.
    descriptor.PageSize_DescControl |=
        (uint32_t)(page_size << DESC_PAGE_SIZE_SHIFT) & (uint32_t)DESC_PAGE_SIZE_MASK;

    if (VdmaInterruptsDomain::NONE != interrupts_domain) {
        // update the desc_control
        descriptor.PageSize_DescControl |= (DESC_REQUREST_IRQ_PROCESSED | DESC_REQUREST_IRQ_ERR);
#ifndef NDEBUG
        descriptor.PageSize_DescControl |= (DESC_STATUS_REQ | DESC_STATUS_REQ_ERR);
#endif
        descriptor.PageSize_DescControl |= get_interrupts_bitmask(interrupts_domain);
    }

    // Clear status
    descriptor.RemainingPageSize_Status = 0;
}

} /* namespace hailort */
