/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_utils.hpp
 * @brief Utilities for file operations
 **/

#ifndef _HAILO_FILE_UTILS_HPP_
#define _HAILO_FILE_UTILS_HPP_

#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

namespace hailort
{

/**
 * Returns the amount of data left in the given file.
 */
Expected<size_t> get_istream_size(std::ifstream &s);

/**
 * Reads full file content into a `Buffer`
 */
Expected<Buffer> read_binary_file(const std::string &file_path,
    const BufferStorageParams &output_buffer_params = {});

// This class is an RAII to return to the original stream position
class StreamPositionGuard
{
public:
    static Expected<StreamPositionGuard> create(std::shared_ptr<std::ifstream> stream);
    ~StreamPositionGuard();

private:
    StreamPositionGuard(std::shared_ptr<std::ifstream> stream, std::streampos beg_pos);

    std::shared_ptr<std::ifstream> m_stream;
    const std::streampos m_beg_pos;
};

class FileReader;
class BufferReader;

class SeekableBytesReader
{
public:
    virtual ~SeekableBytesReader() = default;
    virtual hailo_status read(uint8_t *buffer, size_t n) = 0;
    virtual hailo_status read_from_offset(uint64_t offset, MemoryView dst, size_t n) = 0;
    virtual hailo_status open() = 0;
    virtual bool is_open() const = 0;
    virtual hailo_status seek(size_t position) = 0;
    virtual Expected<size_t> tell() = 0;
    virtual hailo_status close() = 0;
    virtual Expected<size_t> get_size() = 0;
    virtual Expected<bool> good() const = 0;
    virtual Expected<size_t> calculate_remaining_size() = 0;
    static Expected<std::shared_ptr<FileReader>> create_reader(const std::string &file_path);
    static Expected<std::shared_ptr<BufferReader>> create_reader(const MemoryView &memview);
};

class FileReader : public SeekableBytesReader
{
public:
    FileReader(const std::string &file_path);

    virtual hailo_status read(uint8_t *buffer, size_t n);
    virtual hailo_status read_from_offset(uint64_t offset, MemoryView dst, size_t n);
    virtual hailo_status open();
    virtual bool is_open() const;
    virtual hailo_status seek(size_t position);
    virtual Expected<size_t> tell();
    virtual hailo_status close();
    virtual Expected<size_t> get_size();
    virtual Expected<bool> good() const;
    virtual Expected<size_t> calculate_remaining_size();

    std::shared_ptr<std::ifstream> get_fstream() const;

private:
    std::shared_ptr<std::ifstream> m_fstream = nullptr;
    std::string m_file_path;

};

class BufferReader : public SeekableBytesReader
{
public:
    BufferReader(const MemoryView &memview);

    virtual hailo_status read(uint8_t *buffer, size_t n);
    virtual hailo_status read_from_offset(uint64_t offset, MemoryView dst, size_t n);
    virtual hailo_status open();
    virtual bool is_open() const;
    virtual hailo_status seek(size_t position);
    virtual Expected<size_t> tell();
    virtual hailo_status close();
    virtual Expected<size_t> get_size();
    virtual Expected<bool> good() const;
    virtual Expected<size_t> calculate_remaining_size();

    const MemoryView get_memview() const;

private:
    MemoryView m_memview;
    size_t m_seek_offset = 0;
};

} /* namespace hailort */

#endif /* _HAILO_FILE_UTILS_HPP_ */
