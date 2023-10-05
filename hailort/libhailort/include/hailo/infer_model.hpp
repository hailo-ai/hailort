/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model.hpp
 * @brief Async Infer
 **/

#ifndef _HAILO_ASYNC_INFER_HPP_
#define _HAILO_ASYNC_INFER_HPP_

#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"

#include <condition_variable>
#include <mutex>

/** hailort namespace */
namespace hailort
{

class ConfiguredInferModelImpl;
class HAILORTAPI AsyncInferJob
{
public:
    AsyncInferJob() = default;
    virtual ~AsyncInferJob();

    AsyncInferJob(const AsyncInferJob &other) = delete;
    AsyncInferJob &operator=(const AsyncInferJob &other) = delete;
    AsyncInferJob(AsyncInferJob &&other);
    AsyncInferJob &operator=(AsyncInferJob &&other);

    hailo_status wait(std::chrono::milliseconds timeout);
    void detach();

private:
    friend class ConfiguredInferModelImpl;

    class Impl;
    AsyncInferJob(std::shared_ptr<Impl> pimpl);
    std::shared_ptr<Impl> m_pimpl;
    bool m_should_wait_in_dtor;
};

struct CompletionInfoAsyncInfer;
class HAILORTAPI ConfiguredInferModel
{
public:
    class HAILORTAPI Bindings
    {
    public:
        class HAILORTAPI InferStream
        {
        public:
            hailo_status set_buffer(MemoryView view);
            MemoryView get_buffer();

        private:
            friend class ConfiguredInferModelImpl;

            class Impl;
            InferStream(std::shared_ptr<Impl> pimpl);
            std::shared_ptr<Impl> m_pimpl;
        };

        Expected<InferStream> input();
        Expected<InferStream> output();
        Expected<InferStream> input(const std::string &name);
        Expected<InferStream> output(const std::string &name);

    private:
        friend class ConfiguredInferModelImpl;

        Bindings(std::unordered_map<std::string, InferStream> &&inputs,
            std::unordered_map<std::string, InferStream> &&outputs);

        std::unordered_map<std::string, Bindings::InferStream> m_inputs;
        std::unordered_map<std::string, Bindings::InferStream> m_outputs;
    };

    Expected<Bindings> create_bindings();
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);
    hailo_status activate();
    void deactivate();
    hailo_status run(Bindings bindings, std::chrono::milliseconds timeout);
    Expected<AsyncInferJob> run_async(Bindings bindings,
        std::function<void(const CompletionInfoAsyncInfer &)> callback = [] (const CompletionInfoAsyncInfer &) {});

private:
    friend class InferModel;

    ConfiguredInferModel(std::shared_ptr<ConfiguredInferModelImpl> pimpl);

    std::shared_ptr<ConfiguredInferModelImpl> m_pimpl;
};

struct HAILORTAPI CompletionInfoAsyncInfer
{
    CompletionInfoAsyncInfer(ConfiguredInferModel::Bindings _bindings, hailo_status _status) : bindings(_bindings), status(_status)
    {
    }

    ConfiguredInferModel::Bindings bindings;
    hailo_status status;
};

class HAILORTAPI InferModel final
{
public:
    ~InferModel() = default;

    class HAILORTAPI InferStream
    {
    public:
        const std::string name() const;
        size_t get_frame_size() const;
        void set_format_type(hailo_format_type_t type);
        void set_format_order(hailo_format_order_t order);

    private:
        friend class InferModel;
        friend class VDeviceBase;

        class Impl;
        InferStream(std::shared_ptr<Impl> pimpl);
        hailo_format_t get_user_buffer_format();

        std::shared_ptr<Impl> m_pimpl;
    };

    Expected<ConfiguredInferModel> configure(const std::string &network_name = "");
    Expected<InferStream> input();
    Expected<InferStream> output();
    Expected<InferStream> input(const std::string &name);
    Expected<InferStream> output(const std::string &name);
    const std::vector<InferStream> &inputs() const;
    const std::vector<InferStream> &outputs() const;
    const std::vector<std::string> &get_input_names() const;
    const std::vector<std::string> &get_output_names() const;
    
    InferModel(InferModel &&);

private:
    friend class VDeviceBase;

    InferModel(VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferStream> &&inputs,
        std::unordered_map<std::string, InferStream> &&outputs);

    std::reference_wrapper<VDevice> m_vdevice;
    Hef m_hef;
    std::unordered_map<std::string, InferStream> m_inputs;
    std::unordered_map<std::string, InferStream> m_outputs;
    std::vector<InferStream> m_inputs_vector;
    std::vector<InferStream> m_outputs_vector;
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_HPP_ */
