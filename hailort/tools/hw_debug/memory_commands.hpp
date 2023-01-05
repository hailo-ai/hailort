/**
 * @file memory_commands.hpp
 * @brief Commands to access (read/write) some memory (for example - channel registers, descriptors, physical, etc.)
 */

#ifndef _HW_DEBUG_MEMORY_COMMANDS_H_
#define _HW_DEBUG_MEMORY_COMMANDS_H_

#include "shell.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "spdlog/fmt/fmt.h"

#include <sstream>
#include <iomanip>
#include <cinttypes>
#include <map>

class MemorySource;

class Field {
public:
    explicit Field(std::string &&name, std::string &&description);
    virtual ~Field() = default;

    Field(const Field &other) = delete;
    Field &operator=(const Field &other) = delete;

    const std::string &name() const;
    const std::string &description() const;

    virtual size_t elements_count() const = 0;
    virtual std::string print_element(MemorySource& memory, size_t index) const = 0;
private:
    const std::string m_name;
    const std::string m_description;
};

class MemorySource {
public:
    virtual ~MemorySource() = default;

    virtual hailo_status read(uint64_t offset, uint8_t *data, size_t size) = 0;
    virtual hailo_status write(uint64_t offset, const uint8_t *data, size_t size) = 0;
    virtual size_t total_size() const = 0;

    template<typename T>
    T read(uint64_t offset)
    {
        static_assert(std::is_trivial<T>::value, "Non trivial type");
        T value{};
        auto status = read(offset, reinterpret_cast<uint8_t*>(&value), sizeof(value));
        if (HAILO_SUCCESS != status) {
            throw std::runtime_error(fmt::format("Failed read at {} (size {})", offset, sizeof(value)));
        }
        return value;
    }

    const std::map<std::string, std::shared_ptr<Field>> &get_fields() const;
protected:
    void add_field(std::shared_ptr<Field> field);

private:
    std::map<std::string, std::shared_ptr<Field>> m_fields;
};

template<typename IntType>
class MemoryWriteCommand : public ShellCommand {
public:
    static_assert(std::is_integral<IntType>::value, "MemoryWriteCommand works only with integers");

    MemoryWriteCommand(std::shared_ptr<MemorySource> memory) :
        ShellCommand(get_name(), get_short_name(), get_help()),
        m_memory(memory)
    {}

    ShellResult execute(const std::vector<std::string> &args) {
        if (args.size() != 2) {
            return ShellResult("Invalid params\n");
        }

        uint64_t offset;
        if (sscanf(args[0].c_str(), "%" SCNx64, &offset) != 1) {
            return ShellResult(fmt::format("Invalid offset {}\n"));
        }

        uint32_t data;
        if (sscanf(args[1].c_str(), "%" SCNx32, &data) != 1) {
            return ShellResult(fmt::format("Invalid data {}\n", args[1]));
        }

        if ((offset % sizeof(IntType)) != 0) {
            return ShellResult(fmt::format("Offset {:x} must be a multiple of {}\n", offset, sizeof(IntType)));
        }

        if (offset + sizeof(IntType) > m_memory->total_size()) {
            return ShellResult(fmt::format("Offset {:x} too large (max {:x})\n", offset, m_memory->total_size()));
        }

        if (data > std::numeric_limits<IntType>::max()) {
            return ShellResult(fmt::format("data {:x} too large\n", data));
        }
        IntType data_as_int = static_cast<IntType>(data);
        auto status = m_memory->write(offset, reinterpret_cast<uint8_t*>(&data_as_int), sizeof(data_as_int));   
        if (HAILO_SUCCESS != status) {
            return ShellResult(fmt::format("Failed write memory {}\n", status));
        }

        return ShellResult("");
    }

private:
    std::shared_ptr<MemorySource> m_memory;

    static size_t get_bits() { return sizeof(IntType) * 8; }
    static std::string get_name() { return fmt::format("write{}", get_bits()); }
    static std::string get_short_name() { return fmt::format("w{}", get_bits()); }
    static std::string get_help()
    {
        return fmt::format("Writes memory in {} granularity. Usage: {} <offset> <data>. Offset and data are hex integers.", get_bits(),
            get_name());
    }
};

template<typename IntType>
class MemoryReadCommand : public ShellCommand {
public:
    static_assert(std::is_integral<IntType>::value, "MemoryReadCommand works only with integers");

    MemoryReadCommand(std::shared_ptr<MemorySource> memory) :
        ShellCommand(get_name(), get_short_name(), get_help()),
        m_memory(memory)
    {}

    ShellResult execute(const std::vector<std::string> &args) {
        if (args.size() != 2) {
            return ShellResult("Invalid params\n");
        }

        uint64_t offset;
        if (sscanf(args[0].c_str(), "%" SCNx64, &offset) != 1) {
            return ShellResult(fmt::format("Invalid offset {}\n", args[0]));
        }

        uint32_t size;
        if (sscanf(args[1].c_str(), "%" SCNx32, &size) != 1) {
            return ShellResult(fmt::format("Invalid size {}\n", args[1]));
        }

        if ((offset % sizeof(IntType)) != 0) {
            return ShellResult(fmt::format("Offset {:x} must be a multiple of {}\n", offset, sizeof(IntType)));
        }

        if ((size % sizeof(IntType)) != 0) {
            return ShellResult(fmt::format("Size {:x} must be a multiple of {}\n", size, sizeof(IntType)));
        }

        if (offset + size > m_memory->total_size()) {
            return ShellResult(fmt::format("Offset {:x} and size {:x} too large (max {:x})\n", offset, size,
                m_memory->total_size()));
        }

        std::vector<uint8_t> data(size, 0);
        auto status = m_memory->read(offset, data.data(), data.size());
        if (HAILO_SUCCESS != status) {
            return ShellResult(fmt::format("Failed read memory {}\n", status));
        }

        std::stringstream result;
        result << std::hex << std::setfill('0');
        for (size_t i = 0; i < size; i += sizeof(IntType)) {
            if ((i % 16) == 0) {
                // Print address
                result << std::endl << std::setw(8) << (offset + i) << "\t";
            }
            IntType *ptr = reinterpret_cast<IntType*>(data.data() + i);
            result << " " << std::setw(sizeof(IntType) * 2) << static_cast<uint32_t>(*ptr);
        }
        result << std::endl;
        return result.str();
    }

private:
    std::shared_ptr<MemorySource> m_memory;

    static size_t get_bits() { return sizeof(IntType) * 8; }
    static std::string get_name() { return fmt::format("read{}", get_bits()); }
    static std::string get_short_name() { return fmt::format("r{}", get_bits()); }
    static std::string get_help()
    {
        return fmt::format("Reads memory in {} granularity. Usage: {} <offset> <size>. Offset and size are hex integers.",
            get_bits(), get_name());
    }
};

class PrintCommand : public ShellCommand {
public:
    PrintCommand(std::shared_ptr<MemorySource> memory);
    virtual ShellResult execute(const std::vector<std::string> &args) override;

private:
    // Returns pair of field name and the index
    static std::pair<std::string, size_t> parse_field(const std::string &field_arg);
    static std::string get_help(const std::map<std::string, std::shared_ptr<Field>> &fields);

    std::shared_ptr<MemorySource> m_memory;

    static constexpr size_t PRINT_ALL = std::numeric_limits<size_t>::max();
};

#endif /* _HW_DEBUG_MEMORY_COMMANDS_H_ */
