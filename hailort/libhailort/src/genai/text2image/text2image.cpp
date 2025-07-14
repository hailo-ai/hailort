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
#include "hailo/hailort_defaults.hpp"
#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "common/genai/connection_ports.hpp"
#include "genai/genai_common.hpp"
#include "common/genai/serializer/serializer.hpp"
namespace hailort
{
namespace genai
{

constexpr std::chrono::milliseconds Text2ImageGenerator::DEFAULT_OPERATION_TIMEOUT;

constexpr uint32_t TEXT2IMAGE_MIN_SAMPLES_COUNT = 1;
constexpr uint32_t TEXT2IMAGE_MIN_STEPS_COUNT = 1;
constexpr float32_t TEXT2IMAGE_MIN_GUIDANCE_SCALE = 1.0f;

hailo_status validate_hef_path(const std::string &hef_path)
{
    CHECK(((BUILTIN == hef_path) || Filesystem::does_file_exists(hef_path)), HAILO_OPEN_FILE_FAILURE,
        "Hef file {} does not exist", hef_path);

    return HAILO_SUCCESS;
}

Text2ImageParams::Text2ImageParams() :
    m_denoise_hef(""),
    m_text_encoder_hef(""),
    m_image_decoder_hef(""),
    m_ip_adapter_hef(""),
    m_scheduler_type(HailoDiffuserSchedulerType::EULER_DISCRETE)
{}

hailo_status Text2ImageParams::set_denoise_model(const std::string &hef_path)
{
    m_denoise_hef = hef_path;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_text_encoder_model(const std::string &hef_path)
{
    m_text_encoder_hef = hef_path;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_image_decoder_model(const std::string &hef_path)
{
    m_image_decoder_hef = hef_path;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Text2ImageParams::set_ip_adapter_model(const std::string &hef_path)
{
    m_ip_adapter_hef = hef_path;

    auto status = validate_hef_path(hef_path);
    CHECK_SUCCESS(status);

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

const std::string& Text2ImageParams::text_encoder_hef() const
{
    return m_text_encoder_hef;
}


const std::string& Text2ImageParams::image_decoder_hef() const
{
    return m_image_decoder_hef;
}

const std::string& Text2ImageParams::ip_adapter_hef() const
{
    return m_ip_adapter_hef;
}

HailoDiffuserSchedulerType Text2ImageParams::scheduler() const
{
    return m_scheduler_type;
}

hailo_status Text2ImageGeneratorParams::set_samples_count(uint32_t samples_count)
{
    m_samples_count = samples_count;
    return HAILO_SUCCESS;
}

uint32_t Text2ImageGeneratorParams::samples_count() const
{
    return m_samples_count;
}

hailo_status Text2ImageGeneratorParams::set_steps_count(uint32_t steps_count)
{
    m_steps_count = steps_count;
    return HAILO_SUCCESS;
}

uint32_t Text2ImageGeneratorParams::steps_count() const
{
    return m_steps_count;
}

hailo_status Text2ImageGeneratorParams::set_guidance_scale(float32_t guidance_scale)
{
    m_guidance_scale = guidance_scale;
    return HAILO_SUCCESS;
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

Expected<Text2Image> Text2Image::create(std::shared_ptr<VDevice> vdevice, const Text2ImageParams &params)
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
    auto generator_params = m_default_generator_params;
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

uint32_t Text2Image::input_noise_frame_size() const
{
    return m_pimpl->input_noise_frame_size();
}

hailo_3d_image_shape_t Text2Image::input_noise_shape() const
{
    return m_pimpl->input_noise_shape();
}

hailo_format_type_t Text2Image::input_noise_format_type() const
{
    return m_pimpl->input_noise_format_type();
}

hailo_format_order_t Text2Image::input_noise_format_order() const
{
    return m_pimpl->input_noise_format_order();
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

Text2Image::Impl::Impl(std::shared_ptr<SessionWrapper> session, const Text2ImageParams &params, const Text2ImageGeneratorParams &default_generator_params,
    const frame_info_t &output_sample_frame_info, const bool is_ip_adapter_supported, const frame_info_t &input_noise_frame_info, const frame_info_t &ip_adapter_frame_info) :
    m_session(session),
    m_params(params),
    m_default_generator_params(default_generator_params),
    m_output_sample_frame_info(output_sample_frame_info),
    m_has_ip_adapter(is_ip_adapter_supported),
    m_ip_adapter_frame_info(ip_adapter_frame_info),
    m_input_noise_frame_info(input_noise_frame_info)
{}

Text2Image::Impl::~Impl()
{
    auto status = text2image_release();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to release Text2Image with status {}", status);
    }
}

bool Text2Image::Impl::all_builtin_hefs(const Text2ImageParams &params)
{
    auto text2image_res = (params.denoise_hef() == BUILTIN) &&
        (params.text_encoder_hef() == BUILTIN) &&
        (params.image_decoder_hef() == BUILTIN);

    auto ip_adapter_res = (params.ip_adapter_hef() == BUILTIN) || (params.ip_adapter_hef().empty());

    return text2image_res && ip_adapter_res;
}

bool Text2Image::Impl::has_builtin_hef(const Text2ImageParams &params)
{
    return (params.denoise_hef() == BUILTIN) ||
        (params.text_encoder_hef() == BUILTIN) ||
        (params.image_decoder_hef() == BUILTIN) ||
        (params.ip_adapter_hef() == BUILTIN);
}

hailo_status Text2Image::Impl::validate_params(const Text2ImageParams &params)
{
    CHECK(!params.denoise_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `denoise_hef` was not set.");
    CHECK(!params.text_encoder_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `text_encoder_hef` was not set.");
    CHECK(!params.image_decoder_hef().empty(), HAILO_INVALID_OPERATION, "Failed to create Text2Image model. `image_decoder_hef` was not set.");

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<Text2Image::Impl>> Text2Image::Impl::create_unique(std::shared_ptr<VDevice> vdevice, const Text2ImageParams &text2image_params)
{
    CHECK_SUCCESS(validate_params(text2image_params));

    bool is_builtin = all_builtin_hefs(text2image_params);
    CHECK(is_builtin || !has_builtin_hef(text2image_params), HAILO_INVALID_OPERATION,
        "Failed to create Text2Image model. If one of the hefs is set to `BUILTIN`, all the hefs must be set to `BUILTIN`.");

    auto vdevice_params = vdevice->get_params();
    CHECK_SUCCESS(GenAICommon::validate_genai_vdevice_params(vdevice_params));
    std::string device_id = (nullptr != vdevice_params.device_ids) ? vdevice_params.device_ids[0].id : "";
    TRY(auto hailo_session, Session::connect(DEFAULT_TEXT2IMAGE_CONNECTION_PORT, device_id));

    auto session = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto reply, text2image_create(session, vdevice_params, is_builtin, text2image_params));
    TRY(auto default_generator_params, text2image_get_generator_params(session));

    frame_info_t output_sample_frame_info = {};
    output_sample_frame_info.shape = reply.output_frame_shape;
    output_sample_frame_info.format = reply.output_frame_format;

    frame_info_t input_noise_frame_info = {};
    input_noise_frame_info.shape = reply.input_noise_frame_shape;
    input_noise_frame_info.format = reply.input_noise_frame_format;

    bool has_ip_adapter = !text2image_params.ip_adapter_hef().empty();
    frame_info_t ip_adapter_frame_info = {};
    if (has_ip_adapter) {
        TRY(auto ip_adapter_info, text2image_get_ip_adapter_info(session));
        ip_adapter_frame_info.shape = ip_adapter_info.first;
        ip_adapter_frame_info.format = ip_adapter_info.second;
    }

    auto text2image_ptr = make_unique_nothrow<Impl>(session, text2image_params, default_generator_params,
        output_sample_frame_info, has_ip_adapter, input_noise_frame_info, ip_adapter_frame_info);
    CHECK_NOT_NULL_AS_EXPECTED(text2image_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return text2image_ptr;
}

Expected<uint32_t> Text2Image::Impl::ip_adapter_frame_size() const
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_frame_size`. Ip Adapter was not set");

    return HailoRTCommon::get_frame_size(m_ip_adapter_frame_info.shape, m_ip_adapter_frame_info.format);
}

Expected<hailo_3d_image_shape_t> Text2Image::Impl::ip_adapter_shape() const
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_shape`. Ip Adapter was not set");

    auto ip_adapter_frame_shape = m_ip_adapter_frame_info.shape;
    return ip_adapter_frame_shape;
}

Expected<hailo_format_type_t> Text2Image::Impl::ip_adapter_format_type() const
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_format_type`. Ip Adapter was not set");
    auto type = m_ip_adapter_frame_info.format.type;
    return type;
}

Expected<hailo_format_order_t> Text2Image::Impl::ip_adapter_format_order() const
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION,
        "Failed to get `ip_adapter_format_order`. Ip Adapter was not set");

    auto order = m_ip_adapter_frame_info.format.order;
    return order;
}

uint32_t Text2Image::Impl::input_noise_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_input_noise_frame_info.shape, m_input_noise_frame_info.format);
}

hailo_3d_image_shape_t Text2Image::Impl::input_noise_shape() const
{
    return m_input_noise_frame_info.shape;
}

hailo_format_type_t Text2Image::Impl::input_noise_format_type() const
{
    return m_input_noise_frame_info.format.type;
}

hailo_format_order_t Text2Image::Impl::input_noise_format_order() const
{
    return m_input_noise_frame_info.format.order;
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

Expected<std::vector<int>> Text2Image::tokenize(const std::string &prompt)
{
    return m_pimpl->tokenize(prompt);
}

Expected<std::vector<int>> Text2Image::Impl::tokenize(const std::string &prompt)
{
    TRY(auto request, Text2ImageTokenizeSerializer::serialize_request(prompt));
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to tokenize prompt");
    TRY(auto reply, m_session->read());
    TRY(auto tokens, Text2ImageTokenizeSerializer::deserialize_reply(MemoryView(*reply)));
    return tokens;
}

Expected<Text2ImageGenerator> Text2Image::Impl::create_generator(const Text2ImageGeneratorParams &params)
{
    CHECK_SUCCESS(validate_generator_params(params));
    CHECK_SUCCESS(text2image_generator_create(params));

    // TODO: HRT-15869 - adjust to be initialized with a getter from server in creation time
    auto ip_adapter_size = 0;
    if (m_has_ip_adapter) {
        TRY(ip_adapter_size, ip_adapter_frame_size());
    }
    auto pimpl = std::make_unique<Text2ImageGenerator::Impl>(m_session, params, output_sample_frame_size(),
        m_has_ip_adapter, ip_adapter_size);
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);
    return Text2ImageGenerator(std::move(pimpl));
}

Text2ImageGenerator::Impl::Impl(std::shared_ptr<SessionWrapper> session, const Text2ImageGeneratorParams &params,
    uint32_t output_sample_frame_size, bool is_ip_adapter_supported, uint32_t ip_adapter_frame_size) :
        m_session(session),
        m_params(params),
        m_output_sample_frame_size(output_sample_frame_size),
        m_has_ip_adapter(is_ip_adapter_supported),
        m_ip_adapter_frame_size(ip_adapter_frame_size)
{}

hailo_status Text2ImageGenerator::set_initial_noise(const MemoryView &noise)
{
    return m_pimpl->set_initial_noise(noise);
}

hailo_status Text2ImageGenerator::Impl::set_initial_noise(const MemoryView &noise)
{
    TRY(auto request, Text2ImageGeneratorSetInitialNoiseSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(request)), "Failed to write initial noise");
    CHECK_SUCCESS(m_session->write(noise), "Failed to write initial noise");
    TRY(auto reply, m_session->read());
    CHECK_SUCCESS(Text2ImageGeneratorSetInitialNoiseSerializer::deserialize_reply(MemoryView(*reply)),
        "Failed to set initial noise. Make sure the shape and type of the noise buffer are as expected");
    return HAILO_SUCCESS;
}

hailo_status Text2Image::Impl::validate_generator_params(const Text2ImageGeneratorParams &params)
{
    CHECK_AS_EXPECTED(TEXT2IMAGE_MIN_SAMPLES_COUNT <= params.samples_count(), HAILO_INVALID_ARGUMENT,
        "samples_count should be at least {}. received: '{}'", TEXT2IMAGE_MIN_SAMPLES_COUNT, params.samples_count());
    CHECK_AS_EXPECTED(TEXT2IMAGE_MIN_STEPS_COUNT <= params.steps_count(), HAILO_INVALID_ARGUMENT,
        "steps_count should be at least {}. received: '{}'", TEXT2IMAGE_MIN_STEPS_COUNT, params.steps_count());
    CHECK_AS_EXPECTED(TEXT2IMAGE_MIN_GUIDANCE_SCALE <= params.guidance_scale(), HAILO_INVALID_ARGUMENT,
        "guidance_scale should be at least {}. received: '{}'", TEXT2IMAGE_MIN_GUIDANCE_SCALE, params.guidance_scale());

    return HAILO_SUCCESS;
}

Expected<std::vector<Buffer>> Text2ImageGenerator::Impl::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION, "`generate` failed. Ip Adapter was not set");
    CHECK(ip_adapter.size() == m_ip_adapter_frame_size, HAILO_INVALID_OPERATION,
        "`generate` failed. IP Aapter frame size is not as expected ({}), got {}", m_ip_adapter_frame_size, ip_adapter.size());

    std::vector<Buffer> result;
    std::vector<MemoryView> output_images_memview;
    result.resize(m_params.samples_count());
    output_images_memview.reserve(m_params.samples_count());

    for (size_t i = 0; i < m_params.samples_count(); i++) {
        TRY(auto buffer, Buffer::create(m_output_sample_frame_size, BufferStorageParams::create_dma()));
        result[i] = std::move(buffer);
        output_images_memview.emplace_back(result[i]);
    }

    auto status = generate(output_images_memview, positive_prompt, negative_prompt, ip_adapter, timeout);
    CHECK_SUCCESS(status);

    return result;
}

hailo_status Text2ImageGenerator::Impl::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, const MemoryView &ip_adapter, std::chrono::milliseconds timeout)
{
    CHECK_AS_EXPECTED(m_has_ip_adapter, HAILO_INVALID_OPERATION, "`generate` failed. Ip Adapter was not set");
    CHECK(ip_adapter.size() == m_ip_adapter_frame_size, HAILO_INVALID_OPERATION,
        "`generate` failed. IP Aapter frame size is not as expected ({}), got {}", m_ip_adapter_frame_size, ip_adapter.size());

    return text2image_generate(output_images, positive_prompt, negative_prompt, timeout, ip_adapter);
}

Expected<std::vector<Buffer>> Text2ImageGenerator::Impl::generate(const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    std::vector<Buffer> result;
    std::vector<MemoryView> output_images_memview;
    result.resize(m_params.samples_count());
    output_images_memview.reserve(m_params.samples_count());

    for (size_t i = 0; i < m_params.samples_count(); i++) {
        TRY(auto buffer, Buffer::create(m_output_sample_frame_size, BufferStorageParams::create_dma()));
        result[i] = std::move(buffer);
        output_images_memview.emplace_back(result[i]);
    }
    
    auto status = generate(output_images_memview, positive_prompt, negative_prompt, timeout);
    CHECK_SUCCESS(status);

    return result;
}

hailo_status Text2ImageGenerator::Impl::generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout)
{
    CHECK_AS_EXPECTED(!m_has_ip_adapter, HAILO_INVALID_OPERATION,
        "`generate` failed. IP Adapter was set but 'generate' was called without IP Adapter input");

    return text2image_generate(output_images, positive_prompt, negative_prompt, timeout);
}

hailo_status Text2ImageGenerator::Impl::text2image_generate(std::vector<MemoryView> &output_images, const std::string &positive_prompt,
    const std::string &negative_prompt, std::chrono::milliseconds timeout, const MemoryView &ip_adapter)
{
    TimeoutGuard timeout_guard(timeout);

    CHECK_AS_EXPECTED(!positive_prompt.empty(), HAILO_INVALID_ARGUMENT, "`generate` failed. `positive_prompt` cannot be empty");
    CHECK(output_images.size() == m_params.samples_count(), HAILO_INVALID_OPERATION,
        "`generate` failed. `output_images` count must be as was set in GeneratorParams' `samples_count`. Expected: {}, got: {}",
        m_params.samples_count(), output_images.size());
    for (auto &out_img : output_images) {
        CHECK(out_img.size() == m_output_sample_frame_size, HAILO_INVALID_OPERATION,
            "`generate` failed. Output sample frame size is not as expected ({}), got {}", m_output_sample_frame_size, out_img.size());
    }

    TRY(auto generate_request, Text2ImageGeneratorGenerateSerializer::serialize_request(!negative_prompt.empty()));
    CHECK_SUCCESS(m_session->write(MemoryView(generate_request)), "Failed to transfer Text2Image generate request");
    
    CHECK_SUCCESS(m_session->write(MemoryView(positive_prompt), timeout_guard.get_remaining_timeout()), "Failed to write positive prompt");
    if (!negative_prompt.empty()) {
        CHECK_SUCCESS(m_session->write(MemoryView(negative_prompt), timeout_guard.get_remaining_timeout()), "Failed to write negative prompt");
    }

    if (m_has_ip_adapter) {
        CHECK_SUCCESS(m_session->write(ip_adapter, timeout_guard.get_remaining_timeout()), "Failed to write IP Adapter input frame");
    }

    int i = 0;
    for (auto &out_img : output_images) {
        TRY(auto sample_status_reply, m_session->read());
        CHECK_SUCCESS(Text2ImageGeneratorGenerateSerializer::deserialize_reply(MemoryView(sample_status_reply)), "Generation of sample {} failed", i);

        LOGGER__INFO("Reading output sample number {}", i);
        TRY(auto bytes_read, m_session->read(out_img, timeout_guard.get_remaining_timeout()));
        CHECK(bytes_read == out_img.size(), HAILO_INTERNAL_FAILURE,
            "Failed to read output sample frame {}. Expected frame size {}, got {}", i, out_img.size(), bytes_read);
        i++;
    }

    TRY(auto generate_reply, m_session->read());
    CHECK_SUCCESS(Text2ImageGeneratorGenerateSerializer::deserialize_reply(MemoryView(generate_reply)), "Text2Image generate failed");

    return HAILO_SUCCESS;
}

hailo_status Text2Image::Impl::send_hef(std::shared_ptr<SessionWrapper> session, const std::string &hef_path)  
{
    TRY(auto file_data, read_binary_file(hef_path, BufferStorageParams::create_dma()));
    CHECK_SUCCESS(session->write(MemoryView(file_data), LONG_TIMEOUT));
    return HAILO_SUCCESS;
}

Expected<std::pair<hailo_3d_image_shape_t, hailo_format_t>> Text2Image::Impl::text2image_get_ip_adapter_info(std::shared_ptr<SessionWrapper> session)
{
    TRY(auto get_ip_adapter_info_request, Text2ImageGetIPAdapterFrameInfoSerializer::serialize_request());
    CHECK_SUCCESS(session->write(MemoryView(get_ip_adapter_info_request)), "Failed to transfer Text2Image get IP Adapter info request");

    TRY(auto get_ip_adapter_info_reply, session->read());
    TRY(auto ip_adapter_info, Text2ImageGetIPAdapterFrameInfoSerializer::deserialize_reply(MemoryView(get_ip_adapter_info_reply)), "Failed to deserialize reply of Text2Image get IP Adapter info");

    return ip_adapter_info;
}

Expected<Text2ImageCreateSerializer::ReplyInfo> Text2Image::Impl::text2image_create(std::shared_ptr<SessionWrapper> session, const hailo_vdevice_params_t &vdevice_params, bool is_builtin, const Text2ImageParams &text2image_params)
{   
    bool is_ip_adapter = !text2image_params.ip_adapter_hef().empty();
    TRY(auto create_text2image_request, Text2ImageCreateSerializer::serialize_request(vdevice_params, is_builtin, is_ip_adapter, text2image_params.scheduler()));
    CHECK_SUCCESS(session->write(MemoryView(create_text2image_request)), "Failed to transfer Text2Image create request");
    
    // Load HEFs
    if (!is_builtin) {
        CHECK_SUCCESS(send_hef(session, text2image_params.text_encoder_hef()), "Failed to load Text2Image `text_encoder_hef`");
        CHECK_SUCCESS(send_hef(session, text2image_params.denoise_hef()), "Failed to load Text2Image `denoise_hef`");
        CHECK_SUCCESS(send_hef(session, text2image_params.image_decoder_hef()), "Failed to load Text2Image `image_decoder_hef`");
        if (is_ip_adapter) {
            CHECK_SUCCESS(send_hef(session, text2image_params.ip_adapter_hef()), "Failed to load Text2Image `ip_adapter_hef`");
        }
    }

    TRY(auto create_text2image_reply, session->read(LONG_TIMEOUT));
    TRY(auto reply, Text2ImageCreateSerializer::deserialize_reply(MemoryView(create_text2image_reply)));
    
    return reply;
}

Expected<Text2ImageGeneratorParams> Text2Image::Impl::text2image_get_generator_params(std::shared_ptr<SessionWrapper> session)
{
    TRY(auto request, Text2ImageGetGeneratorParamsSerializer::serialize_request());
    CHECK_SUCCESS(session->write(MemoryView(request)), "Failed to get Text2Image generator params");

    TRY_V(auto reply, session->read());
    TRY(auto generator_params, Text2ImageGetGeneratorParamsSerializer::deserialize_reply(MemoryView(*reply)));

    return generator_params;
}

hailo_status Text2Image::Impl::text2image_generator_create(const Text2ImageGeneratorParams &params)
{
    TRY(auto create_generator_request, Text2ImageGeneratorCreateSerializer::serialize_request(params));
    CHECK_SUCCESS(m_session->write(MemoryView(create_generator_request)), "Failed to transfer Text2Image generator create request");
    
    TRY(auto create_generator_reply, m_session->read());
    CHECK_SUCCESS(Text2ImageGeneratorCreateSerializer::deserialize_reply(MemoryView(create_generator_reply)), "Failed to deserialize reply of Text2Image generator create");
    
    return HAILO_SUCCESS;
}

hailo_status Text2Image::Impl::text2image_release()
{
    TRY(auto release_request, Text2ImageReleaseSerializer::serialize_request());
    CHECK_SUCCESS(m_session->write(MemoryView(release_request)), "Failed to transfer Text2Image release request");

    TRY(auto release_reply, m_session->read());
    CHECK_SUCCESS(Text2ImageReleaseSerializer::deserialize_reply(MemoryView(release_reply)), "Failed to deserialize reply of Text2Image release");

    return HAILO_SUCCESS;
}

// https://stackoverflow.com/questions/71104545/constructor-and-destructor-in-c-when-using-the-pimpl-idiom
// All member functions shoud be implemented in the cpp module
Text2Image::~Text2Image() = default;
Text2Image::Text2Image(Text2Image &&) = default;

Text2ImageGenerator::~Text2ImageGenerator() = default;
Text2ImageGenerator::Text2ImageGenerator(Text2ImageGenerator &&) = default;

} /* namespace genai */
} /* namespace hailort */
