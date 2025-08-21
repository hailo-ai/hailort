/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file text2image_server.hpp
 * @brief Implementation for Text2Image server
 **/

#ifndef _HAILO_HAILO_GENAI_TEXT2IMAGE_SERVER_HPP_
#define _HAILO_HAILO_GENAI_TEXT2IMAGE_SERVER_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/hailo_session.hpp"
#include "hailo/genai/text2image/text2image.hpp"

#include "common/utils.hpp"

#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

#include "inference_manager.hpp"
#include "text_encoder.hpp"
#include "single_io_model.hpp"

namespace hailort
{
namespace genai
{

enum class Text2ImageModelType {
    TEXT_ENCODER,
    DENOISE,
    IMAGE_DECODER,
    IP_ADAPTER,
    SUPER_RESOLUTION
};

class Denoiser;

class Text2ImageServer final
{
public:
    // TODO: HRT-16824 - Get from hef
    static constexpr uint32_t TEXT2IMAGE_SAMPLES_COUNT_DEFAULT_VALUE = 1;
    static constexpr uint32_t TEXT2IMAGE_STEPS_COUNT_DEFAULT_VALUE = 20;
    static constexpr float32_t TEXT2IMAGE_GUIDANCE_SCALE_DEFAULT_VALUE = 7.5f;

    // TODO: (HRT-18240) Consider using a base class for all servers
    enum class State {
        READY,
        GENERATING,
        ABORTING, // Indicate that the generation is being aborted by the client
        ERROR, // Indicate some internal error in the server, or something that makes it unusable
    };

    Text2ImageServer(std::shared_ptr<Session> session, std::shared_ptr<Event> shutdown_event,
        std::shared_ptr<std::atomic<State>> state, const std::string &generation_session_listener_ip = "");
    ~Text2ImageServer();

    hailo_status flow();

    Expected<Buffer> handle_create_text2image_request(const MemoryView &request);
    Expected<Buffer> handle_create_generator_request(const MemoryView &request);
    Expected<Buffer> handle_get_generator_params_request(const MemoryView &request);
    Expected<Buffer> handle_get_ip_adapter_frame_info_request(const MemoryView &request);
    Expected<Buffer> handle_tokenize_request(const MemoryView &request);
    Expected<Buffer> handle_set_initial_noise_request(const MemoryView &request);
    Expected<Buffer> handle_abort_request(const MemoryView &request);

    // Note: This function is not executed within the dispatcher thread.
    //
    // The generation process is blocking — the generate function returns to the user
    // only after the entire generation is complete.
    //
    // To support calling abort() while generation is in progress, the generation must run
    // in a separate thread from the main request-handling thread. This allows generation
    // and abort() to execute concurrently.
    Expected<Buffer> handle_generate_request(const MemoryView &request);

private:
    void init_generation_thread(const std::string &generation_session_listener_ip);
    hailo_status init_denoiser(std::shared_ptr<hailort::VDevice> vdevice, HailoDiffuserSchedulerType scheduler_type, bool is_builtin);
    hailo_status init_text_encoder(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin);
    hailo_status init_ip_adapter(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin);
    hailo_status init_image_decoder(std::shared_ptr<hailort::VDevice> vdevice, bool is_builtin);
    hailo_status init_super_resolution(std::shared_ptr<hailort::VDevice> vdevice);

    hailo_status connect_pipeline_buffers();
    Expected<Hef> get_hef(Text2ImageModelType model, bool is_builtin);
    Expected<std::shared_ptr<Buffer>> get_builitin_hef_buffer(Text2ImageModelType model);
    hailo_status handle_ip_adapter_frame();
    hailo_status run_generation_cycle();
    hailo_status check_abort_state();
    hailo_status abort_generation();

    SessionWrapper m_session;
    SessionWrapper m_generation_session;
    
    std::shared_ptr<std::atomic<State>> m_state;
    std::mutex m_generation_mutex;
    std::condition_variable m_cv;

    std::thread m_generation_thread;
    std::shared_ptr<Event> m_shutdown_event;
    
    bool m_has_super_resolution;
    HailoDiffuserSchedulerType m_scheduler_type;
    Text2ImageGeneratorParams m_generator_params;

    std::unique_ptr<TextEncoder> m_text_encoder;
    std::unique_ptr<SingleIOModel> m_ip_adapter;
    std::unique_ptr<Denoiser> m_denoiser;
    std::unique_ptr<SingleIOModel> m_image_decoder;
    std::unique_ptr<SingleIOModel> m_super_resolution;

    BufferPtr m_output_result; // TODO: HRT-17086 - Remove and use the model's output MemoryView
};

// TODO (HRT-18240): Make this class generic for all genai servers
class Text2ImageServerManager
{
public:
    static Expected<std::unique_ptr<Text2ImageServerManager>> create(std::shared_ptr<Session> session, const std::string &generation_session_listener_ip = "");

    hailo_status flow();

    Text2ImageServerManager(std::shared_ptr<Session> session, std::unique_ptr<Text2ImageServer> &&server);

protected:
    SessionWrapper m_session;
    std::unique_ptr<Text2ImageServer> m_server;
    std::array<std::function<Expected<Buffer>(const MemoryView &)>, static_cast<size_t>(HailoGenAIActionID::GENAI_ACTIONS_COUNT)> m_dispatcher;

};

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_HAILO_GENAI_TEXT2IMAGE_SERVER_HPP_ */
