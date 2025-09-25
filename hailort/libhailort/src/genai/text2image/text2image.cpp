/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image.cpp
 * @brief Text2Image Implementation
 **/

#include "genai/text2image/text2image_internal.hpp"
#include "hailo/genai/common.hpp"
#include "hailo/hef.hpp"
#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"
#include "common/filesystem.hpp"


namespace hailort
{
namespace genai
{

/*! Indicates if the action of the current `write`, was successful on server side */
const std::string SERVER_SUCCESS_ACK = "<success>";
constexpr std::chrono::milliseconds Text2ImageGenerator::DEFAULT_OPERATION_TIMEOUT;
constexpr uint16_t DEFAULT_TEXT2IMAGE_CONNECTION_PORT = 12144;

constexpr uint32_t TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE = 1;
constexpr uint32_t TEXT2IMAGE_STEPS_COUNT_DEFAULT_VALUE = 20;
constexpr float32_t TEXT2IMAGE_GUIDANCE_SCALE_DEFAULT_VALUE = 7.5f;
constexpr uint32_t TEXT2IMAGE_SEED_DEFAULT_VALUE = 0;

hailo_status write_and_validate_ack(std::shared_ptr<GenAISession> session, uint8_t *buffer,
    size_t size, std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT)
{
    TimeoutGuard timeout_guard(timeout);
    auto status = session->write(MemoryView(buffer, size), timeout_guard.get_remaining_timeout());
    CHECK_SUCCESS(status);

    TRY(auto ack, session->get_ack(timeout_guard.get_remaining_timeout()));
    CHECK((ack.find(SERVER_SUCCESS_ACK) != std::string::npos), HAILO_INTERNAL_FAILURE, "Transfer failed, got error: {}", ack);
    LOGGER__INFO("Received ack from server - '{}'", ack);

    return HAILO_SUCCESS;
}

hailo_status write_and_validate_ack(std::shared_ptr<GenAISession> session, MemoryView buffer,
    std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT)
{
    return write_and_validate_ack(session, buffer.data(), buffer.size(), timeout);
}

hailo_status validate_hef_path(const std::string &hef_path)
{
    CHECK(((BUILTIN == hef_path) || Filesystem::does_file_exists(hef_path)), HAILO_OPEN_FILE_FAILURE,
        "Hef file {} does not exist", hef_path);

    return HAILO_SUCCESS;
}

Text2ImageParams::Text2ImageParams() :
    m_denoise_hef(""),
    m_denoise_lora(""),
    m_text_encoder_hef(""),
    m_text_encoder_lora(""),
    m_image_decoder_hef(""),
    m_image_decoder_lora(""),
    m_ip_adapter_hef(""),
    m_ip_adapter_lora(""),
    m_scheduler_type(HailoDiffuserSchedulerType::EULER)
{}

hailo_status Text2ImageParams::set_denoise_model(const std::string &hef_path, const std::string &lora_name)
{
    m_denoise_hef = hef_path;
    m_denoise_lora = lora_name;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);
    CHECK(lora_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting LoRA is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_text_encoder_model(const std::string &hef_path, const std::string &lora_name)
{
    m_text_encoder_hef = hef_path;
    m_text_encoder_lora = lora_name;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);
    CHECK(lora_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting LoRA is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_image_decoder_model(const std::string &hef_path, const std::string &lora_name)
{
    m_image_decoder_hef = hef_path;
    m_image_decoder_lora = lora_name;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);
    CHECK(lora_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting LoRA is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_ip_adapter_model(const std::string &hef_path, const std::string &lora_name)
{
    m_ip_adapter_hef = hef_path;
    m_ip_adapter_lora = lora_name;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);
    CHECK(lora_name.empty(), HAILO_NOT_IMPLEMENTED, "Setting LoRA is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_scheduler(HailoDiffuserSchedulerType scheduler_type)
{
    m_scheduler_type = scheduler_type;
    return HAILO_SUCCESS;
}

const std::string& Text2ImageParams::denoise_hef() const
{
    return m_denoise_hef;
}

const std::string& Text2ImageParams::denoise_lora() const
{
    return m_denoise_lora;
}

const std::string& Text2ImageParams::text_encoder_hef() const
{
    return m_text_encoder_hef;
}

const std::string& Text2ImageParams::text_encoder_lora() const
{
    return m_text_encoder_lora;
}

const std::string& Text2ImageParams::image_decoder_hef() const
{
    return m_image_decoder_hef;
}

const std::string& Text2ImageParams::image_decoder_lora() const
{
    return m_image_decoder_lora;
}

const std::string& Text2ImageParams::ip_adapter_hef() const
{
    return m_ip_adapter_hef;
}

const std::string& Text2ImageParams::ip_adapter_lora() const
{
    return m_ip_adapter_lora;
}

HailoDiffuserSchedulerType Text2ImageParams::scheduler() const
{
    return m_scheduler_type;
}

hailo_status Text2ImageGeneratorParams::set_samples_count(uint32_t samples_count)
{
    m_samples_count = samples_count;

    LOGGER__ERROR("`set_samples_count` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

uint32_t Text2ImageGeneratorParams::samples_count() const
{
    return m_samples_count;
}

hailo_status Text2ImageGeneratorParams::set_steps_count(uint32_t steps_count)
{
    m_steps_count = steps_count;

    LOGGER__ERROR("`set_steps_count` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

uint32_t Text2ImageGeneratorParams::steps_count() const
{
    return m_steps_count;
}

hailo_status Text2ImageGeneratorParams::set_guidance_scale(float32_t guidance_scale)
{
    m_guidance_scale = guidance_scale;

    LOGGER__ERROR("`set_guidance_scale` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

float32_t Text2ImageGeneratorParams::guidance_scale() const
{
    return m_guidance_scale;
}

hailo_status Text2ImageGeneratorParams::set_seed(uint32_t seed)
{
    m_seed = seed;

    return HAILO_SUCCESS;
}

uint32_t Text2ImageGeneratorParams::seed() const
{
    return m_seed;
}

Text2Image::Text2Image(std::unique_ptr<Text2Image::Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

Expected<Text2Image> Text2Image::create(std::shared_ptr<VDeviceGenAI> vdevice, const Text2ImageParams &params)
{
    TRY(auto pimpl, Text2Image::Impl::create_unique(vdevice, params));
    return Text2Image(std::move(pimpl));
}

Expected<Text2ImageGenerator> Text2Image::create_generator(const Text2ImageGeneratorParams &params)
{
    return m_pimpl->create_generator(params);
}

Expected<Text2ImageGenerator> Text2Image::create_generator()
{
    TRY(auto generator_params, create_generator_params());
    return m_pimpl->create_generator(generator_params);
}

Expected<Text2ImageGeneratorParams> Text2Image::create_generator_params()
{
    return m_pimpl->create_generator_params();
}

Expected<Text2ImageGeneratorParams> Text2Image::Impl::create_generator_params()
{
    // TODO: Use a getter from server
    auto generator_params = hailort::genai::Text2ImageGeneratorParams();
    generator_params.m_samples_count = TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE;
    generator_params.m_steps_count = TEXT2IMAGE_STEPS_COUNT_DEFAULT_VALUE;
    generator_params.m_guidance_scale = TEXT2IMAGE_GUIDANCE_SCALE_DEFAULT_VALUE;
    generator_params.m_seed = TEXT2IMAGE_SEED_DEFAULT_VALUE;

    return generator_params;
}

uint32_t Text2Image::output_sample_frame_size() const
{
    return m_pimpl->output_sample_frame_size();
}

hailo_3d_image_shape_t Text2Image::output_sample_shape() const
{
    return m_pimpl->output_sample_shape();
}

hailo_format_type_t Text2Image::output_sample_format_type() const
{
    return m_pimpl->output_sample_format_type();
}

hailo_format_order_t Text2Image::output_sample_format_order() const
{
    return m_pimpl->output_sample_format_order();
}

Expected<uint32_t> Text2Image::ip_adapter_frame_size() const
{
    return m_pimpl->ip_adapter_frame_size();
}

Expected<hailo_3d_image_shape_t> Text2Image::ip_adapter_shape() const
{
    return m_pimpl->ip_adapter_shape();
}

Expected<hailo_format_type_t> Text2Image::ip_adapter_format_type() const
{
    return m_pimpl->ip_adapter_format_type();
}

Expected<hailo_format_order_t> Text2Image::ip_adapter_format_order() const
{
    return m_pimpl->ip_adapter_format_order();
}

Text2ImageGenerator::Text2ImageGenerator(std::unique_ptr<Text2ImageGenerator::Impl> pimpl) :
    m_pimpl(std::move(pimpl))
{}

Expected<std::vector<Buffer>> Text2ImageGenerator::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate(positive_prompt, negative_prompt, timeout);
}

Expected<std::vector<Buffer>> Text2ImageGenerator::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate(positive_prompt, negative_prompt, ip_adapter, timeout);
}

hailo_status Text2ImageGenerator::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate(output_images, positive_prompt, negative_prompt, timeout);
}

hailo_status Text2ImageGenerator::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    return m_pimpl->generate(output_images, positive_prompt, negative_prompt, ip_adapter, timeout);
}

hailo_status Text2ImageGenerator::stop()
{
    return m_pimpl->stop();
}

Text2Image::Impl::Impl(std::shared_ptr<GenAISession> session, const Text2ImageParams &params,
    const frame_info_t &output_sample_frame_info, const bool is_ip_adapter_supported, const frame_info_t &ip_adapter_frame_info) :
    m_session(session),
    m_params(params),
    m_output_sample_frame_info(output_sample_frame_info),
    m_is_ip_adapter_supported(is_ip_adapter_supported),
    m_ip_adapter_frame_info(ip_adapter_frame_info)
{}

hailo_status Text2Image::Impl::validate_params(const Text2ImageParams &params)
{
    CHECK(!params.denoise_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `denoise_hef` was not set.");
    CHECK(!params.text_encoder_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `text_encoder_hef` was not set.");
    CHECK(!params.image_decoder_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `image_decoder_hef` was not set.");

    // TODO: HRT-15973 - Remove after supporting no IP Adapter flow
    CHECK(!params.ip_adapter_hef().empty(), HAILO_NOT_IMPLEMENTED,
        "Failed to create Text2Image model. `ip_adapter_hef` was not set. Running without `ip_adapter` is not implemented yet.");

    CHECK(params.denoise_lora().empty(), HAILO_NOT_IMPLEMENTED, "Failed to create Text2Image model. Setting `denoise_lora` is not implemented yet.");
    CHECK(params.text_encoder_lora().empty(), HAILO_NOT_IMPLEMENTED, "Failed to create Text2Image model. Setting `text_encoder_lora` is not implemented yet.");
    CHECK(params.image_decoder_lora().empty(), HAILO_NOT_IMPLEMENTED, "Failed to create Text2Image model. Setting `image_decoder_lora` is not implemented yet.");
    CHECK(params.ip_adapter_lora().empty(), HAILO_NOT_IMPLEMENTED, "Failed to create Text2Image model. Setting `ip_adapter_lora` is not implemented yet.");

    return HAILO_SUCCESS;
}

hailo_status Text2Image::Impl::load_params(std::shared_ptr<GenAISession> session, const Text2ImageParams &params)
{
    auto status = session->send_file(params.denoise_hef());
    CHECK_SUCCESS(status, "Failed to load Text2Image `denoise_hef`");

    status = session->send_file(params.text_encoder_hef());
    CHECK_SUCCESS(status, "Failed to load Text2Image `text_encoder_hef`");

    status = session->send_file(params.image_decoder_hef());
    CHECK_SUCCESS(status, "Failed to load Text2Image `image_decoder_hef`");

    // TODO: HRT-15799 - Adjust sending ip_adapter (if necessary) according to server integration
    status = session->send_file(params.ip_adapter_hef());
    CHECK_SUCCESS(status, "Failed to load Text2Image `ip_adapter_hef`");

    auto scheduler_type = params.scheduler();
    status = write_and_validate_ack(session, reinterpret_cast<uint8_t*>(&scheduler_type), sizeof(scheduler_type));
    CHECK_SUCCESS(status, "Failed to configure Text2Image scheduler type");

    // Ack from server - finished configuring model
    TRY(auto ack, session->get_ack());
    LOGGER__INFO("Received ack from server - '{}'", ack);

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<Text2Image::Impl>> Text2Image::Impl::create_unique(std::shared_ptr<VDeviceGenAI> vdevice, const Text2ImageParams &params)
{
    CHECK_SUCCESS(validate_params(params));

    TRY(auto session, vdevice->create_session(DEFAULT_TEXT2IMAGE_CONNECTION_PORT));
    CHECK_SUCCESS(load_params(session, params));

    // Output sample info (TODO: HRT-15869 - Get info from server side)
    frame_info_t output_sample_frame_info = {};
    output_sample_frame_info.format = {HAILO_FORMAT_TYPE_UINT8, HAILO_FORMAT_ORDER_NHWC, HAILO_FORMAT_FLAGS_NONE};
    output_sample_frame_info.shape = {1024, 1024, 3};

    bool is_ip_adapter_supported = false;
    frame_info_t ip_adapter_frame_info = {};
    if (!params.ip_adapter_hef().empty()) {
        // Input Ip Adapter info (TODO: HRT-15869 - Get info from server side)
        is_ip_adapter_supported = true;
        ip_adapter_frame_info.format = {HAILO_FORMAT_TYPE_UINT8, HAILO_FORMAT_ORDER_NHWC, HAILO_FORMAT_FLAGS_NONE};
        ip_adapter_frame_info.shape = {112, 112, 3};
    }

    auto text2image = Impl(session, params, output_sample_frame_info, is_ip_adapter_supported, ip_adapter_frame_info);
    auto text2image_ptr = std::make_unique<Impl>(std::move(text2image));
    CHECK_NOT_NULL_AS_EXPECTED(text2image_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return text2image_ptr;
}

Expected<uint32_t> Text2Image::Impl::ip_adapter_frame_size() const
{
    CHECK_AS_EXPECTED(m_is_ip_adapter_supported, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_frame_size`. Ip Adapter was not set");

    return HailoRTCommon::get_frame_size(m_ip_adapter_frame_info.shape, m_ip_adapter_frame_info.format);
}

Expected<hailo_3d_image_shape_t> Text2Image::Impl::ip_adapter_shape() const
{
    CHECK_AS_EXPECTED(m_is_ip_adapter_supported, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_shape`. Ip Adapter was not set");

    auto ip_adapter_frame_shape = m_ip_adapter_frame_info.shape;
    return ip_adapter_frame_shape;
}

Expected<hailo_format_type_t> Text2Image::Impl::ip_adapter_format_type() const
{
    CHECK_AS_EXPECTED(m_is_ip_adapter_supported, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_format_type`. Ip Adapter was not set");
    auto type = m_ip_adapter_frame_info.format.type;
    return type;
}

Expected<hailo_format_order_t> Text2Image::Impl::ip_adapter_format_order() const
{
    CHECK_AS_EXPECTED(m_is_ip_adapter_supported, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_format_order`. Ip Adapter was not set");

    auto order = m_ip_adapter_frame_info.format.order;
    return order;
}

uint32_t Text2Image::Impl::output_sample_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_output_sample_frame_info.shape, m_output_sample_frame_info.format);
}

hailo_3d_image_shape_t Text2Image::Impl::output_sample_shape() const
{
    return m_output_sample_frame_info.shape;
}

hailo_format_type_t Text2Image::Impl::output_sample_format_type() const
{
    return m_output_sample_frame_info.format.type;
}

hailo_format_order_t Text2Image::Impl::output_sample_format_order() const
{
    return m_output_sample_frame_info.format.order;
}

Expected<Text2ImageGenerator> Text2Image::Impl::create_generator(const Text2ImageGeneratorParams &params)
{
    CHECK_SUCCESS(validate_generator_params(params));
    CHECK_SUCCESS(load_generator_params(params));

    TRY(auto ip_adapter_frame_size, ip_adapter_frame_size());
    auto pimpl = std::make_unique<Text2ImageGenerator::Impl>(m_session, params, output_sample_frame_size(),
        m_is_ip_adapter_supported, ip_adapter_frame_size);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return Text2ImageGenerator(std::move(pimpl));
}

Text2ImageGenerator::Impl::Impl(std::shared_ptr<GenAISession> session, const Text2ImageGeneratorParams &params,
    uint32_t output_sample_frame_size, bool is_ip_adapter_supported, uint32_t ip_adapter_frame_size) :
        m_session(session),
        m_params(params),
        m_output_sample_frame_size(output_sample_frame_size),
        m_is_ip_adapter_supported(is_ip_adapter_supported),
        m_ip_adapter_frame_size(ip_adapter_frame_size)
{}

hailo_status Text2Image::Impl::validate_generator_params(const Text2ImageGeneratorParams &params)
{
    CHECK_AS_EXPECTED(TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE == params.samples_count(), HAILO_NOT_IMPLEMENTED,
        "Setting generator's samples_count is not implemented.");
    CHECK_AS_EXPECTED(TEXT2IMAGE_STEPS_COUNT_DEFAULT_VALUE == params.steps_count(), HAILO_NOT_IMPLEMENTED,
        "Setting generator's steps_count is not implemented.");
    CHECK_AS_EXPECTED(TEXT2IMAGE_GUIDANCE_SCALE_DEFAULT_VALUE == params.guidance_scale(), HAILO_NOT_IMPLEMENTED,
        "Setting generator's guidance_scale is not implemented.");

    return HAILO_SUCCESS;
}

hailo_status Text2Image::Impl::load_generator_params(const Text2ImageGeneratorParams &params)
{
    // TODO: Use serialization functions
    text2image_generator_params_t packed_params = {
        params.steps_count(),
        params.samples_count(),
        params.guidance_scale(),
        params.seed()
    };

    auto status = write_and_validate_ack(m_session, reinterpret_cast<uint8_t*>(&packed_params), sizeof(packed_params));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::vector<Buffer>> Text2ImageGenerator::Impl::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    // TODO: HRT-15972 - Create `m_samples_count` buffers
    std::vector<MemoryView> output_images_memview;
    TRY(auto buffer, Buffer::create(m_output_sample_frame_size, BufferStorageParams::create_dma()));
    output_images_memview.emplace_back(buffer);

    auto status = generate(output_images_memview, positive_prompt, negative_prompt, ip_adapter, timeout);
    CHECK_SUCCESS(status);

    // TODO: HRT-15972 - use m_params.samples_count()
    (void)m_params; // not used yet, added to fix android compilation
    std::vector<Buffer> result;
    result.reserve(TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE);
    result.emplace_back(std::move(buffer));

    return result;
}

hailo_status Text2ImageGenerator::Impl::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    TimeoutGuard timeout_guard(timeout);

    CHECK_AS_EXPECTED(!positive_prompt.empty(), HAILO_INVALID_ARGUMENT, "`generate` failed. `positive_prompt` cannot be empty");
    CHECK_AS_EXPECTED(m_is_ip_adapter_supported, HAILO_INVALID_OPERATION, "`generate` failed. Ip Adapter was not set");
    CHECK(ip_adapter.size() == m_ip_adapter_frame_size, HAILO_INVALID_OPERATION,
        "`generate` failed. IP Aapter frame size is not as expected ({}), got {}", m_ip_adapter_frame_size, ip_adapter.size());
    CHECK(output_images.size() == TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE, HAILO_INVALID_OPERATION,
        "`generate` failed. Samples count must be {}, got {}", TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE, output_images.size());
    for (auto &out_img : output_images) {
        CHECK(out_img.size() == m_output_sample_frame_size, HAILO_INVALID_OPERATION,
            "`generate` failed. Output sample frame size is not as expected ({}), got {}", m_output_sample_frame_size, out_img.size());
    }

    // Send info before generation
    text2image_generation_info_t packed_info = {};
    packed_info.has_negative_prompt = !negative_prompt.empty();
    packed_info.has_ip_adapter = true;

    auto status = write_and_validate_ack(m_session, reinterpret_cast<uint8_t*>(&packed_info), sizeof(packed_info), timeout_guard.get_remaining_timeout());
    CHECK_SUCCESS(status);

    status = write_and_validate_ack(m_session, positive_prompt, timeout_guard.get_remaining_timeout());
    CHECK_SUCCESS(status);

    if (packed_info.has_negative_prompt) {
        status = write_and_validate_ack(m_session, negative_prompt, timeout_guard.get_remaining_timeout());
        CHECK_SUCCESS(status);
    }

    status = write_and_validate_ack(m_session, ip_adapter, timeout_guard.get_remaining_timeout());
    CHECK_SUCCESS(status);

    for (auto &out_img : output_images) {
        TRY(auto bytes_read, m_session->read(out_img, timeout_guard.get_remaining_timeout()));
        CHECK(bytes_read == out_img.size(), HAILO_INTERNAL_FAILURE,
            "Failed to read output sample frame. Expected frame size {}, got {}", out_img.size(), bytes_read);
    }

    return HAILO_SUCCESS;
}

hailo_status Text2ImageGenerator::Impl::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    (void)output_images;
    (void)positive_prompt;
    (void)negative_prompt;
    (void)timeout;

    LOGGER__ERROR("`Text2ImageGenerator::generate()` function without ip-adapter is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<std::vector<Buffer>> Text2ImageGenerator::Impl::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    (void)positive_prompt;
    (void)negative_prompt;
    (void)timeout;

    LOGGER__ERROR("`Text2ImageGenerator::generate()` function without ip-adapter is not supported yet");
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status Text2ImageGenerator::Impl::stop()
{
    LOGGER__ERROR("`Text2ImageGenerator::stop()` function is not supported yet");
    return HAILO_NOT_IMPLEMENTED;
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
Text2Image::~Text2Image() = default;
Text2Image::Text2Image(Text2Image &&) = default;

Text2ImageGenerator::~Text2ImageGenerator() = default;
Text2ImageGenerator::Text2ImageGenerator(Text2ImageGenerator &&) = default;

} /* namespace genai */
} /* namespace hailort */
