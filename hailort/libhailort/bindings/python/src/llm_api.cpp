/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_api.cpp
 * @brief Defines binding to an LLM class family usage over Python.
 **/
 #include "llm_api.hpp"
 #include "bindings_common.hpp"
 
 using namespace hailort;

LLMGeneratorCompletionWrapper::LLMGeneratorCompletionWrapper(std::unique_ptr<genai::LLMGeneratorCompletion> completion) :
    m_completion(std::move(completion))
{}

void LLMGeneratorCompletionWrapper::release()
{
    m_completion.reset();
}

std::string LLMGeneratorCompletionWrapper::read(uint32_t timeout)
{
    auto expected = m_completion->read(std::chrono::milliseconds(timeout));
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

std::string LLMGeneratorCompletionWrapper::read_all(uint32_t timeout)
{
    auto expected = m_completion->read_all(std::chrono::milliseconds(timeout));
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

genai::LLMGeneratorCompletion::Status LLMGeneratorCompletionWrapper::generation_status() const
{
    return m_completion->generation_status();
}

void LLMGeneratorCompletionWrapper::bind(py::module &m)
{
    py::class_<LLMGeneratorCompletionWrapper, std::shared_ptr<LLMGeneratorCompletionWrapper>>(m, "LLMGeneratorCompletionWrapper")
        .def("read", &LLMGeneratorCompletionWrapper::read)
        .def("read_all", &LLMGeneratorCompletionWrapper::read_all)
        .def("generation_status", &LLMGeneratorCompletionWrapper::generation_status)
        .def("release", &LLMGeneratorCompletionWrapper::release);
}

LLMWrapper LLMWrapper::create(VDeviceWrapperPtr vdevice, const std::string &model_path,
    const std::string &lora_name, bool optimize_memory_on_device)
{
    auto llm_params = hailort::genai::LLMParams(model_path, lora_name, optimize_memory_on_device);
    auto llm = genai::LLM::create(vdevice->m_vdevice, llm_params);
    VALIDATE_EXPECTED(llm);

    auto llm_ptr = std::make_unique<genai::LLM>(llm.release());

    return LLMWrapper(std::move(llm_ptr));
}

LLMWrapper::LLMWrapper(std::unique_ptr<genai::LLM> llm) :
    m_llm(std::move(llm))
{}

genai::LLMGeneratorParams LLMWrapper::create_generator_params()
{
    auto params = m_llm->create_generator_params();
    VALIDATE_EXPECTED(params);
    return params.release();
}

std::shared_ptr<LLMGeneratorCompletionWrapper> LLMWrapper::generate(const genai::LLMGeneratorParams &params,
    const std::string &prompt)
{
    auto generator = m_llm->create_generator(params);
    VALIDATE_EXPECTED(generator);
    VALIDATE_STATUS(generator->write(prompt));
    auto completion = generator->generate();
    VALIDATE_EXPECTED(completion);
    auto completion_ptr = std::make_unique<genai::LLMGeneratorCompletion>(completion.release());
    return std::make_shared<LLMGeneratorCompletionWrapper>(std::move(completion_ptr));
}

std::shared_ptr<LLMGeneratorCompletionWrapper> LLMWrapper::generate(const genai::LLMGeneratorParams &params,
    const std::vector<std::string> &prompt_json_strings)
{
    auto completion = m_llm->generate(params, prompt_json_strings);
    VALIDATE_EXPECTED(completion);
    auto completion_ptr = std::make_unique<genai::LLMGeneratorCompletion>(completion.release());
    return std::make_shared<LLMGeneratorCompletionWrapper>(std::move(completion_ptr));
}

std::vector<int> LLMWrapper::tokenize(const std::string &prompt)
{
    auto expected = m_llm->tokenize(prompt);
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

size_t LLMWrapper::get_context_usage_size()
{
    auto count = m_llm->get_context_usage_size();
    VALIDATE_EXPECTED(count);
    return count.release();
}

size_t LLMWrapper::max_context_capacity()
{
    auto capacity = m_llm->max_context_capacity();
    VALIDATE_EXPECTED(capacity);
    return capacity.release();
}

void LLMWrapper::clear_context()
{
    VALIDATE_STATUS(m_llm->clear_context());
}

std::string LLMWrapper::get_generation_recovery_sequence()
{
    auto expected = m_llm->get_generation_recovery_sequence();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

void LLMWrapper::set_generation_recovery_sequence(const std::string &sequence)
{
    VALIDATE_STATUS(m_llm->set_generation_recovery_sequence(sequence));
}

std::string LLMWrapper::prompt_template()
{
    auto expected = m_llm->prompt_template();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

void LLMWrapper::set_stop_tokens(const std::vector<std::string> &stop_tokens)
{
    VALIDATE_STATUS(m_llm->set_stop_tokens(stop_tokens));
}

std::vector<std::string> LLMWrapper::get_stop_tokens()
{
    auto expected = m_llm->get_stop_tokens();
    VALIDATE_EXPECTED(expected);
    return expected.release();
}

std::vector<uint8_t> LLMWrapper::save_context()
{
    auto expected = m_llm->save_context();
    VALIDATE_EXPECTED(expected);
    auto buffer = expected.release();
    // TODO (HRT-19106): Avoid copying the data to the Python side
    return std::vector<uint8_t>(buffer->data(), buffer->data() + buffer->size());
}

void LLMWrapper::load_context(const std::vector<uint8_t> &context)
{
    MemoryView context_view(const_cast<uint8_t*>(context.data()), context.size());
    VALIDATE_STATUS(m_llm->load_context(context_view));
}

void LLMWrapper::release()
{
    m_llm.reset();
}

void LLMWrapper::bind(py::module &m)
{
    py::class_<LLMWrapper, std::shared_ptr<LLMWrapper>>(m, "LLMWrapper")
        .def("create", &LLMWrapper::create)
        .def("release", &LLMWrapper::release)
        .def("create_generator_params", &LLMWrapper::create_generator_params)
        .def("generate", static_cast<std::shared_ptr<LLMGeneratorCompletionWrapper>(LLMWrapper::*)(const genai::LLMGeneratorParams&, const std::string&)>(&LLMWrapper::generate))
        .def("generate", static_cast<std::shared_ptr<LLMGeneratorCompletionWrapper>(LLMWrapper::*)(const genai::LLMGeneratorParams&, const std::vector<std::string>&)>(&LLMWrapper::generate))
        .def("tokenize", &LLMWrapper::tokenize)
        .def("clear_context", &LLMWrapper::clear_context)
        .def("get_context_usage_size", &LLMWrapper::get_context_usage_size)
        .def("max_context_capacity", &LLMWrapper::max_context_capacity)
        .def("get_generation_recovery_sequence", &LLMWrapper::get_generation_recovery_sequence)
        .def("set_generation_recovery_sequence", &LLMWrapper::set_generation_recovery_sequence)
        .def("prompt_template", &LLMWrapper::prompt_template)
        .def("set_stop_tokens", &LLMWrapper::set_stop_tokens)
        .def("get_stop_tokens", &LLMWrapper::get_stop_tokens)
        .def("save_context", &LLMWrapper::save_context)
        .def("load_context", &LLMWrapper::load_context)
        ;
}