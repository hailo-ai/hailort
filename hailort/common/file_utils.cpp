/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file file_utils.cpp
 * @brief Utilities for file operations
 **/

#include "common/file_utils.hpp"
#include "common/utils.hpp"
#include <fstream>

namespace hailort
{

Expected<size_t> get_istream_size(std::ifstream &s)
{
    auto beg_pos = s.tellg();
    CHECK_AS_EXPECTED(-1 != beg_pos, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    s.seekg(0, s.end);
    CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    auto size = s.tellg();
    CHECK_AS_EXPECTED(-1 != size, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    s.seekg(beg_pos, s.beg);
    CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    auto total_size = static_cast<size_t>(size - beg_pos);
    CHECK_AS_EXPECTED(total_size <= std::numeric_limits<size_t>::max(), HAILO_FILE_OPERATION_FAILURE,
        "File size {} is too big", total_size);
    return Expected<size_t>(static_cast<size_t>(total_size));
}

Expected<size_t> get_istream_size(const std::string &file_path)
{
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    CHECK(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", file_path);

    return get_istream_size(file);
}

Expected<size_t> read_binary_file(const std::string &file_path, MemoryView mem_view)
{
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    CHECK(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", file_path);

    TRY(auto file_size, get_istream_size(file), "Failed to get file size");

    CHECK(mem_view.size() >= file_size, HAILO_INSUFFICIENT_BUFFER, "Provided mem_view size {} is too small for file size {}",
        mem_view.size(), file_size);

    // Read the data
    file.read(reinterpret_cast<char*>(mem_view.data()), file_size);
    CHECK(file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading file {}. errno: {}", file_path, errno);
    return file_size;
}

Expected<Buffer> read_binary_file(const std::string &file_path, const BufferStorageParams &output_buffer_params)
{
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    CHECK(file.good(), HAILO_OPEN_FILE_FAILURE, "Error opening file {}", file_path);

    TRY(const auto file_size, get_istream_size(file), "Failed to get file size");
    TRY(auto buffer, Buffer::create(file_size, output_buffer_params),
        "Failed to allocate file buffer ({} bytes}", file_size);

    TRY(auto ret_file_size, read_binary_file(file_path, buffer.as_view()));
    (void) ret_file_size;

    return buffer;
}

Expected<size_t> read_device_file(const std::string &file_path, MemoryView buffer)
{
    std::ifstream file(file_path);
    if (!file) {
        return make_unexpected(HAILO_OPEN_FILE_FAILURE);
    }

    size_t bytes_read = 0;
    while (file && (bytes_read < buffer.size())) {
        file.read(reinterpret_cast<char*>(buffer.data() + bytes_read), (buffer.size() - bytes_read));
        bytes_read += file.gcount();
        if (file.gcount() == 0) {
            break;
        }
    }

    return bytes_read;
}

Expected<std::shared_ptr<StreamPositionGuard>> StreamPositionGuard::create_shared(std::shared_ptr<std::ifstream> stream)
{
    CHECK_AS_EXPECTED((nullptr != stream), HAILO_INVALID_ARGUMENT);

    const auto beg_pos = stream->tellg();
    CHECK_AS_EXPECTED((-1 != beg_pos), HAILO_INTERNAL_FAILURE, "ifstream::tellg() failed");

    auto ptr = std::make_shared<StreamPositionGuard>(stream, beg_pos);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

StreamPositionGuard::StreamPositionGuard(std::shared_ptr<std::ifstream> stream, std::streampos beg_pos) :
    m_stream(stream),
    m_beg_pos(beg_pos)
{}

StreamPositionGuard::~StreamPositionGuard()
{
    // Only try to restore position if stream is still open and in good state
    if (m_stream && m_stream->is_open() && m_stream->good()) {
        m_stream->seekg(m_beg_pos, std::ios::beg);
        if (!m_stream->good()) {
            LOGGER__ERROR("ifstream::seekg() failed");
        }
    }
}

Expected<std::shared_ptr<FileReader>> SeekableBytesReader::create_reader(const std::string &file_path)
{
    auto ptr = make_shared_nothrow<FileReader>(file_path);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

Expected<std::shared_ptr<BufferReader>> SeekableBytesReader::create_reader(const MemoryView &memview)
{
    auto ptr = make_shared_nothrow<BufferReader>(memview);
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

FileReader::FileReader(const std::string &file_path) : m_file_path(file_path) {}

hailo_status FileReader::read(uint8_t *buffer, size_t n)
{
    assert(nullptr != m_fstream);
    (void)m_fstream->read(reinterpret_cast<char*>(buffer), n);
    return m_fstream->good() ? HAILO_SUCCESS : HAILO_FILE_OPERATION_FAILURE;
}

hailo_status FileReader::read_from_offset(uint64_t offset, MemoryView dst, size_t size)
{
    assert(nullptr != m_fstream);

    auto beg_pos = m_fstream->tellg();
    (void)m_fstream->seekg(offset);
    CHECK(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    (void)m_fstream->read(reinterpret_cast<char*>(dst.data()), size);
    CHECK(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::read() failed");

    (void)m_fstream->seekg(beg_pos);
    CHECK(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    return HAILO_SUCCESS;
}

Expected<MemoryView> FileReader::read_from_offset_as_memview(uint64_t offset, size_t size)
{
    (void)offset;
    (void)size;
    LOGGER__ERROR("Reading from offset as memview is not supported when reading from file");
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

hailo_status FileReader::open()
{
    if (nullptr == m_fstream) { // The first call to open creates the ifstream object
        m_fstream = std::make_shared<std::ifstream>(m_file_path, std::ios::in | std::ios::binary);
        CHECK(m_fstream->good(), HAILO_OPEN_FILE_FAILURE, "Failed opening file, path: {}", m_file_path);
        TRY(m_fstream_guard, StreamPositionGuard::create_shared(m_fstream));
    } else {
        m_fstream->open(m_file_path, std::ios::in | std::ios::binary);
        CHECK(m_fstream->good(), HAILO_OPEN_FILE_FAILURE, "Failed opening file, path: {}", m_file_path);
    }

    return HAILO_SUCCESS;
}

bool FileReader::is_open() const
{
    return m_fstream->is_open();
}

hailo_status FileReader::seek(size_t position)
{
    assert(nullptr != m_fstream);
    (void)m_fstream->seekg(position, m_fstream->beg);
    return m_fstream->good() ? HAILO_SUCCESS : HAILO_FILE_OPERATION_FAILURE;
}

Expected<size_t> FileReader::tell()
{
    assert(nullptr != m_fstream);
    auto offset = m_fstream->tellg();
    return m_fstream->good() ? Expected<size_t>(static_cast<size_t>(offset)) : make_unexpected(HAILO_FILE_OPERATION_FAILURE);
}

hailo_status FileReader::close()
{
    assert(nullptr != m_fstream);
    m_fstream->close();
    return m_fstream->good() ? HAILO_SUCCESS : HAILO_CLOSE_FAILURE;
}

Expected<size_t> FileReader::get_size()
{
    assert(nullptr != m_fstream);

    auto beg_pos = m_fstream->tellg();
    CHECK_AS_EXPECTED(-1 != beg_pos, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    (void)m_fstream->seekg(0, m_fstream->end);
    CHECK_AS_EXPECTED(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    auto file_size = m_fstream->tellg();
    CHECK_AS_EXPECTED(-1 != file_size, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    (void)m_fstream->seekg(beg_pos, m_fstream->beg);
    CHECK_AS_EXPECTED(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    return static_cast<size_t>(file_size);
}

std::shared_ptr<std::ifstream> FileReader::get_fstream() const
{
    return m_fstream;
}

Expected<size_t> FileReader::calculate_remaining_size()
{
    assert(nullptr != m_fstream);
    auto remaining_size = get_istream_size(*m_fstream);
    CHECK_AS_EXPECTED(m_fstream->good(), HAILO_FILE_OPERATION_FAILURE, "FileReader::calculate_remaining_size() failed");
    return remaining_size;
}

Expected<bool> FileReader::good() const
{
    assert(nullptr != m_fstream);
    return m_fstream->good();
}

BufferReader::BufferReader(const MemoryView &memview) : m_memview(memview) {}

hailo_status BufferReader::read(uint8_t *buffer, size_t n)
{
    assert(m_seek_offset + n <= m_memview.size());
    memcpy(buffer, m_memview.data() + m_seek_offset, n);
    m_seek_offset += n;
    return HAILO_SUCCESS;
}

hailo_status BufferReader::read_from_offset(uint64_t offset, MemoryView dst, size_t size)
{
    memcpy(dst.data(), m_memview.data() + offset, size);
    return HAILO_SUCCESS;
}

Expected<MemoryView> BufferReader::read_from_offset_as_memview(uint64_t offset, size_t size)
{
    assert(m_memview.data() + offset + size <= m_memview.data() + m_memview.size());
    return MemoryView((m_memview.data() + offset), size);
}

hailo_status BufferReader::open()
{
    // In case we use the buffer, we don't need to check if the file is open
    return HAILO_SUCCESS;
}

bool BufferReader::is_open() const
{
    // In case we use the buffer, we don't need to check if the file is open
    return true;
}

hailo_status BufferReader::seek(size_t position)
{
    assert(position < m_memview.size());
    m_seek_offset = position;
    return HAILO_SUCCESS;
}

Expected<size_t> BufferReader::tell()
{
    return Expected<size_t>(m_seek_offset);
}

hailo_status BufferReader::close()
{
    return HAILO_SUCCESS;
}

Expected<size_t> BufferReader::get_size()
{
    return Expected<size_t>(m_memview.size());
}

Expected<size_t> BufferReader::calculate_remaining_size()
{
    return m_memview.size() - m_seek_offset;
}

Expected<bool> BufferReader::good() const
{
    return true;
}

const MemoryView BufferReader::get_memview() const
{
    return m_memview;
}

} /* namespace hailort */
