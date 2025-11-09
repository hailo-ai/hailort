/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file llm_api.hpp
 * @brief Defines binding to an LLM class family usage over Python.
 **/

 #ifndef LLM_API_HPP_
 #define LLM_API_HPP_
 
 #include "hailo/hailort.h"
 #include "hailo/genai/llm/llm.hpp"
 
 #include "vdevice_api.hpp"
 
 
 namespace hailort {
 
 
class LLMGeneratorCompletionWrapper final
{
public:
    LLMGeneratorCompletionWrapper(std::unique_ptr<genai::LLMGeneratorCompletion> completion);
    void release();

    std::string read(uint32_t timeout);
    std::string read_all(uint32_t timeout);
    genai::LLMGeneratorCompletion::Status generation_status() const;
 
    static void bind(py::module &m);
 
private:
    std::unique_ptr<genai::LLMGeneratorCompletion> m_completion;
};
 

class LLMWrapper final{
public:
    static LLMWrapper create(std::shared_ptr<VDeviceWrapper> vdevice, const std::string &model_path, const std::string &lora_name, bool optimize_memory_on_device = false);

    LLMWrapper(std::unique_ptr<genai::LLM> llm);

    genai::LLMGeneratorParams create_generator_params();

    std::shared_ptr<LLMGeneratorCompletionWrapper> generate(const genai::LLMGeneratorParams &params, const std::string &prompt);
    std::shared_ptr<LLMGeneratorCompletionWrapper> generate(const genai::LLMGeneratorParams &params, const std::vector<std::string> &prompt_json_strings);
    void release();
    std::vector<int> tokenize(const std::string &prompt);
    size_t get_context_usage_size();
    size_t max_context_capacity();
    void clear_context();
    std::string get_generation_recovery_sequence();
    void set_generation_recovery_sequence(const std::string &sequence);
    std::string prompt_template();
    void set_stop_tokens(const std::vector<std::string> &stop_tokens);
    std::vector<std::string> get_stop_tokens();
    std::vector<uint8_t> save_context();
    void load_context(const std::vector<uint8_t> &context);

    static void bind(py::module &m);

private:
    std::unique_ptr<genai::LLM> m_llm;
};
 
 } // namespace hailort
 
 #endif /* LLM_API_HPP_ */