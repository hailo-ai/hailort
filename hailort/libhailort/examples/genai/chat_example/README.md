# LLM Chat Example - Structured Prompts Guide

This example demonstrates how to use Hailo's LLM (Large Language Model) API with structured prompts for interactive chat applications.

## Overview

The chat example showcases:
- **Interactive LLM Chat**: Real-time conversation with language models
- **Structured Prompts**: JSON-based message formatting for better conversation management
- **Template Integration**: Automatic prompt template application
- **Conversation Context**: Persistent conversation history across interactions

## Quick Start

### Prerequisites
- HailoRT installed and configured
- Appropriate LLM HEF file from Hailo Model Zoo GenAI

### Compilation
```bash
# From examples directory
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target chat_example

# Or from chat_example directory
cd genai/chat_example
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```

### Execution
```bash
./build/cpp/chat_example/chat_example <hef_path>
```

## Structured Prompts

### What are Structured Prompts?

Structured prompts use JSON format to organize conversation messages, making interactions more standardized and flexible compared to plain text prompts.

### JSON Message Format

Each message follows this structure:
```json
{
    "role": "user|system|assistant",
    "content": "message content"
}
```

**Roles:**
- **`system`**: System instructions and context setting
- **`user`**: User inputs and queries
- **`assistant`**: Assistant responses (for conversation history)

### Implementation Example

```cpp
// Traditional approach (less flexible)
std::string prompt = "<start>system: You are a helpful assistant<end><start>user: Hello<end><start>assistant:";
auto completion = llm.generate(prompt); // Or generator.write(prompt) && auto completion = generator.generate();

// Structured approach (recommended)
std::vector<std::string> messages = {
    R"({"role": "system", "content": "You are a helpful assistant"})",
    R"({"role": "user", "content": "Hello"})"
};
generator.write(messages);
auto completion = generator.generate();
```

### Multi-Message Conversations
```cpp
std::vector<std::string> conversation = {
    R"({"role": "system", "content": "You are a helpful programming assistant"})",
    R"({"role": "user", "content": "How do I sort an array in Python?"})",
    R"({"role": "assistant", "content": "You can use the sorted() function or .sort() method"})",
    R"({"role": "user", "content": "What's the difference between them?"})"
};
generator.write(conversation); // auto completion = llm.generate(conversation);
```

### Complex Content
```cpp
std::string complex_content = "Explain this code:\\n```python\\nprint('hello')\\n```";
std::vector<std::string> messages = {
    R"({"role": "user", "content": ")" + complex_content + R"("})"
};
```

### Context Management
```cpp
// Clear conversation history
llm.clear_context();

// Continue conversation with fresh context
std::vector<std::string> new_conversation = {
    R"({"role": "user", "content": "Start a new topic"})"
};
```

## Benefits of Structured Prompts

### 1. **Standardization**
- Consistent format across different models and applications
- Compatible with industry standards
- Easier integration with external tools

### 2. **Flexibility**
- Easy role management (system, user, assistant)
- Support for conversation history
- Template-independent message construction

### 3. **Maintainability**
- Clear separation of content and metadata
- Easier debugging and logging
- Better error handling

### 4. **Template Integration**
- Automatic prompt template application
- Model-specific formatting handled internally
- No manual template management required

## Best Practices

1. **Always validate JSON syntax** before sending messages
2. **Use appropriate roles** for different message types
3. **Escape special characters** properly in content
4. **Clear context periodically** for long conversations
5. **Handle errors gracefully** with proper exception handling

## Troubleshooting

**Generation fails after write**
- Check JSON format validity
- Ensure all required fields are present
- Verify HEF file compatibility

**Poor response quality**
- Review system message content
- Check conversation context
- Consider adjusting generator parameters

**Memory issues with long conversations**
- Call `clear_context()` periodically
- Limit conversation history length
- Monitor memory usage patterns

For more examples and advanced usage, see the HailoRT user guide and API documentation.