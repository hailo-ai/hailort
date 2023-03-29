/**
 * @file hailo15_fields.cpp
 * @brief Contains all memory fields related to hailo15
 */

#include "hailo15_fields.hpp"
#include "hw_consts/hailo15/dram_dma_engine_config_regs.h"

// Implement our own offsetof to allow access to array
#define my_offsetof(type,field) ((size_t)(&(((type*)(0))->field)))
#define dram_dma_offsetof(field) my_offsetof(DRAM_DMA_ENGINE_CONFIG_t, field)


static constexpr auto CCB_ADDRESS_SHIFT = 9;


QddcField::QddcField() :
    Field("qddc", "Queue dest device channel (qddc)")
{}

size_t QddcField::elements_count() const
{
    return DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH;
}

std::string QddcField::print_element(MemorySource& memory, size_t index) const
{
    return fmt::format("qddc[{}] enabled={} mode={} shmifo_id={}\n", index,
        is_enabled(memory, index), mode(memory, index), shmifo_id(memory, index));
}

bool QddcField::is_enabled(MemorySource &memory, size_t index) const
{
    return (1 == memory.read<uint32_t>(dram_dma_offsetof(QddcEnable[index])));
}

uint32_t QddcField::shmifo_id(MemorySource &memory, size_t index) const
{
    return memory.read<uint32_t>(dram_dma_offsetof(QddcShmifoId[index]));
}

std::string QddcField::mode(MemorySource &memory, size_t index) const
{
    const auto mode = memory.read<uint32_t>(dram_dma_offsetof(QddcMode[index]));
    switch (mode) {
    case 0: return "CONTINUOUS";
    case 1: return "BURST";
    default:
        return fmt::format("Unknown {}", mode);
    }
}

QsdcField::QsdcField() :
    Field("qsdc", "Queue source device channel (qsdc)")
{}

size_t QsdcField::elements_count() const
{
    return DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH;
}

std::string QsdcField::print_element(MemorySource& memory, size_t index) const
{
    return fmt::format("qsdc[{}] enabled={} shmifo_id={}\n", index,
        is_enabled(memory, index), shmifo_id(memory, index));
}

bool QsdcField::is_enabled(MemorySource &memory, size_t index) const
{
    return (1 == memory.read<uint32_t>(dram_dma_offsetof(QsdcEnable[index])));
}

uint32_t QsdcField::shmifo_id(MemorySource &memory, size_t index) const
{
    return memory.read<uint32_t>(dram_dma_offsetof(QsdcShmifoId[index]));
}

QdmcField::QdmcField() :
    Field("qdmc", "Queue dest memory channel (qdmc)")
{}

size_t QdmcField::elements_count() const
{
    return DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH;
}

std::string QdmcField::print_element(MemorySource& memory, size_t index) const
{
    return fmt::format("qdmc[{}] enabled={} address=0x{:x} desc_count={} desc_per_irq={}\n", index,
        is_enabled(memory, index), base_address(memory, index), descriptors_count(memory, index),
        descriptors_per_irq(memory, index));
}

bool QdmcField::is_enabled(MemorySource &memory, size_t index) const
{
    return (1 == memory.read<uint32_t>(dram_dma_offsetof(QdmcEnable[index])));
}

uint64_t QdmcField::base_address(MemorySource &memory, size_t index) const
{
    const uint64_t address = memory.read<uint32_t>(dram_dma_offsetof(QdmcMemBaseAddr[index]));
    return address << CCB_ADDRESS_SHIFT;
}

uint32_t QdmcField::descriptors_count(MemorySource &memory, size_t index) const
{
    if (index > DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_REGULAR_CH) {
        return memory.read<uint32_t>(dram_dma_offsetof(QdmcMemCcbSize[index - DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_REGULAR_CH]));
    }
    else {
        const auto desc_count_log2 = memory.read<uint32_t>(dram_dma_offsetof(QdmcMemCcbSizeLog2[index]));
        uint32_t size = 1;
        for (uint32_t i = 0; i < desc_count_log2; i++) {
            size <<= 1;
        }
        return size;
    }
}

uint32_t QdmcField::descriptors_per_irq(MemorySource &memory, size_t index) const
{
    return memory.read<uint32_t>(dram_dma_offsetof(QdmcDescCsInterrupt[index]));
}

QsmcField::QsmcField() :
    Field("qsmc", "Queue source memory channel (qsmc)")
{}

size_t QsmcField::elements_count() const
{
    return DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH;
}

std::string QsmcField::print_element(MemorySource& memory, size_t index) const
{
    return fmt::format("qdmc[{}] mode={} enabled={} address=0x{:x} desc_count={}\n", index,
        mode(memory, index), is_enabled(memory, index), base_address(memory, index), descriptors_count(memory, index));
}

bool QsmcField::is_enabled(MemorySource &memory, size_t index) const
{
    return (1 == memory.read<uint32_t>(dram_dma_offsetof(QsmcEnable[index])));
}

uint64_t QsmcField::base_address(MemorySource &memory, size_t index) const
{
    const uint64_t address = memory.read<uint32_t>(dram_dma_offsetof(QsmcMemBaseAddr[index]));
    return address << CCB_ADDRESS_SHIFT;
}

uint32_t QsmcField::descriptors_count(MemorySource &memory, size_t index) const
{
    const auto desc_count = memory.read<uint32_t>(dram_dma_offsetof(QsmcMemCcbSize[index]));
    return desc_count + 1; // The reg contains desc_count-1
}

std::string QsmcField::mode(MemorySource &memory, size_t index) const
{
    const auto mode = memory.read<uint32_t>(dram_dma_offsetof(QsmcMode[index]));
    switch (mode) {
    case 0: return "CONTINUOUS";
    case 2: return "BURST";
    case 3: // C2C mode
    {
        auto c2c_sel = memory.read<uint32_t>(dram_dma_offsetof(QsmcC2cSel[index]));
        return fmt::format("C2C (from {})", c2c_sel);
    }
    default:
        return fmt::format("Unknown {}", mode);
    }
}
