# LLM Chat Example - Raw Prompts Guide

This example demonstrates how to use Hailo’s LLM API with raw (unstructured) prompts to build an interactive chat application where the user fully controls prompt formatting and conversation structure.

The code in this example uses the Qwen2.5-1.5B-Instruct model as provided in Hailo Model Zoo GenAI. Prompt templates and formatting are matched specifically for this model created by Alibaba, but you can adapt them for other models as needed.

## Overview

The raw prompt chat example showcases:
- **Interactive LLM Chat**: Real-time conversation with language models
- **Raw Prompts**: Input text is sent exactly as provided
- **Manual prompt template management**: Full control over roles, tokens, and formatting
- **Conversation Context**: Persistent conversation history across interactions

This example is intended for advanced users who need fine-grained control over prompt construction and model behavior.
If you want automatic role handling and template generation, see the `chat_example`, which uses structured prompts.

## Quick Start

### Prerequisites
- HailoRT installed and configured
- Appropriate LLM HEF file from Hailo Model Zoo GenAI
- Knowledge of the prompt template required for the users model

Note: Raw prompts must match the exact format expected by the model.
Incorrect formatting may result in degraded or incorrect outputs.

### Compilation
```bash
# From examples directory
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target chat_raw_prompt_example

# Or from chat_raw_prompt_example directory
cd genai/chat_raw_prompt_example
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```

### Execution
```bash
./build/cpp/chat_raw_prompt_example/chat_raw_prompt_example
```

## Raw Prompts

### What are Raw Prompts?

A raw prompt is a plain text string sent directly to the model without any automatic formatting or role handling.

### Implementation Example

```cpp
// Define model-specific constants (Qwen2.5-1.5B-Instruct prompt template)
static const std::string MODEL_FIRST_PROMPT_PREFIX = "<|im_start|>system\nYou are a helpful assistant<|im_end|>\n<|im_start|>user\n";
static const std::string MODEL_GENERAL_PROMPT_PREFIX = "<|im_start|>user\n";
static const std::string MODEL_PROMPT_SUFFIX = "<|im_end|>\n<|im_start|>assistant\n";

// First message (includes system prompt)
std::string first_prompt = MODEL_FIRST_PROMPT_PREFIX + user_input + MODEL_PROMPT_SUFFIX;
generator.write(first_prompt);

// Subsequent messages (no system prompt needed)
std::string next_prompt = MODEL_GENERAL_PROMPT_PREFIX + user_input + MODEL_PROMPT_SUFFIX;
generator.write(next_prompt);
```

### Context Management
```cpp
// Clear conversation history
llm.clear_context();

// After clearing, use first prompt prefix again (includes system message)
std::string prompt = MODEL_FIRST_PROMPT_PREFIX + user_input + MODEL_PROMPT_SUFFIX;
```

## Best Practices

1. **Know your model's prompt template** - Refer to the model documentation for required tokens and formatting.
2. **Handle first vs. subsequent prompts** - System messages typically only appear once
3. **Validate formatting early** - Small token mistakes can significantly affect output quality
4. **Clear context appropriately** - Reset to first prompt format after clearing
5. **Clear context periodically** for long conversations
6. **Handle errors gracefully** with proper exception handling

## Related Examples

`chat_example` - Demonstrates structured prompts with automatic role and template handling.
For additional examples and advanced usage, refer to the HailoRT User Guide and GenAI API documentation.