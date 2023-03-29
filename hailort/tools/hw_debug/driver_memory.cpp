/**
 * @file driver_memory.cpp
 * @brief Implements MemorySource over HailoRT driver, reads/write all interfaces.
 */

#include "driver_memory.hpp"
#include "hailo15_fields.hpp"

DriverMemorySource::DriverMemorySource(std::shared_ptr<HailoRTDriver> driver, HailoRTDriver::MemoryType memory_type) :
    m_driver(driver),
    m_memory_type(memory_type)
{}

hailo_status DriverMemorySource::read(uint64_t offset, uint8_t *data, size_t size)
{
    return m_driver->read_memory(m_memory_type, offset, data, size);
}

hailo_status DriverMemorySource::write(uint64_t offset, const uint8_t *data, size_t size)
{
    return m_driver->write_memory(m_memory_type, offset, data, size);
}

size_t DriverMemorySource::total_size() const
{
    // TODO HRT-7984: return the actual size
    return std::numeric_limits<size_t>::max();
}


static constexpr size_t VDMA_CHANNELS_COUNT = 32;
static constexpr size_t VDMA_H2D_CHANNELS_COUNT = 16;

#pragma pack(push, 1)
struct VdmaDataPerDirection {
    // Control
    uint64_t start_abort    : 1;
    uint64_t pause_resume   : 1;
    uint64_t abort_on_err   : 1;
    uint64_t reserved0      : 2;
    uint64_t irq_on_err     : 1;
    uint64_t irq_on_host    : 1;
    uint64_t irq_on_device  : 1;

    // Depth id
    uint64_t id             : 3;
    uint64_t depth          : 4;
    uint64_t reserved1      : 1;

    uint64_t num_available  : 16;
    uint64_t num_processed  : 16;
    uint64_t num_ongoing    : 16;

    uint64_t error          : 8;
    uint64_t reserved2      : 8;
    uint64_t desc_address   : 48;
};
static_assert(0x10 == sizeof(VdmaDataPerDirection), "Invalid VdmaDataPerDirection size");

struct VdmaChannelData {
    VdmaDataPerDirection src;
    VdmaDataPerDirection dest;
};
#pragma pack(pop)

class VdmaChannelField : public Field {
public:
    VdmaChannelField() :
        Field("channel", "vDMA channel register")
    {}

    virtual size_t elements_count() const
    {
        return VDMA_CHANNELS_COUNT;
    };

    virtual std::string print_element(MemorySource& memory, size_t index) const
    {
        assert(index < elements_count());
        VdmaChannelData data{};
        auto status = memory.read(index * sizeof(data), reinterpret_cast<uint8_t*>(&data), sizeof(data));
        if (HAILO_SUCCESS != status) {
            throw std::runtime_error(fmt::format("Failed reading memory, status {}", status));
        }

        return fmt::format("channel[{}] (offset=0x{:X} size=0x{:X} type= {}):\n", index, index * sizeof(data), sizeof(data),
                index < VDMA_H2D_CHANNELS_COUNT ? "H2D" : "D2H") +
               fmt::format("    Src status:  {}\n", print_src_status(data.src)) + 
               fmt::format("    Dest status: {}\n", print_dest_status(data.dest)) + 
               fmt::format("    Src:   {}\n", print_direction(data.src)) +
               fmt::format("    Dest:  {}\n", print_direction(data.dest));
    }

private:
    static std::string print_src_status(const VdmaDataPerDirection &data) {
        auto max_desc_mask =  static_cast<uint16_t>((1 << data.depth)  - 1);
        std::string status =
            data.error ? "CHANNEL ERROR" :
            !data.start_abort ? "ABORTED" :
            data.pause_resume ? "PAUSED" : 
            (data.num_ongoing & max_desc_mask) != (data.num_processed & max_desc_mask) ? "DURING TRANSFER" : 
            (data.num_available & max_desc_mask) != (data.num_processed & max_desc_mask) ? "WAITING TO SEND" : 
                "IDLE";
        return status;
    }

    static std::string print_dest_status(const VdmaDataPerDirection &data) {    
        auto max_desc_mask =  static_cast<uint16_t>((1 << data.depth)  - 1);
        std::string status = 
            data.error ? "CHANNEL ERROR" :
            !data.start_abort ? "ABORTED" :
            data.pause_resume ? "PAUSED" : 
            (data.num_ongoing & max_desc_mask) != (data.num_processed & max_desc_mask) ? "DURING TRANSFER" : 
            (data.num_available & max_desc_mask) != (data.num_processed & max_desc_mask) ? "WAITING TO RECEIVE" :
                "IDLE";
        return status;
    }

    static std::string print_direction(const VdmaDataPerDirection &data)
    {
        return fmt::format(
            "control=({} | {}) id={} depth={:02} num_avail=0x{:04X} num_proc=0x{:04X} num_ongoing=0x{:04X} err=0x{:02X} desc_address=0x{:016X}",
            data.start_abort ? "START" : "ABORT",
            data.pause_resume ? "PAUSE" : "RESUME",
            data.id,
            data.depth,
            data.num_available,
            data.num_processed,
            data.num_ongoing,
            data.error,
            data.desc_address << DESC_ADDRESS_SHIFT);
    }

    static constexpr size_t DESC_ADDRESS_SHIFT = 16;
};

VdmaMemorySource::VdmaMemorySource(std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type) :
    DriverMemorySource(std::move(driver), memory_type)
{
    add_field(std::make_shared<VdmaChannelField>());
}

size_t VdmaMemorySource::total_size() const
{
    return VDMA_CHANNELS_COUNT * sizeof(VdmaChannelData);
}

DramDmaEngineMemorySource::DramDmaEngineMemorySource(std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type) :
    DriverMemorySource(std::move(driver), memory_type)
{
    add_field(std::make_shared<QddcField>());
    add_field(std::make_shared<QsdcField>());
    add_field(std::make_shared<QdmcField>());
    add_field(std::make_shared<QsmcField>());
}