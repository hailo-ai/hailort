# VLM Example - Structured Prompts for Vision-Language Models

This example demonstrates how to use Hailo's VLM (Vision Language Model) API with structured prompts for interactive multi-modal conversations.

## Overview

The VLM example showcases:
- **Multi-modal Conversations**: Seamless integration of text and image content
- **Structured Prompts**: JSON-based message formatting for VLM interactions
- **Automatic Template Processing**: Built-in prompt template application
- **Image Validation**: Automatic verification of image count vs. message content
- **Interactive Interface**: Real-time conversation with vision-language models

## Quick Start

### Prerequisites
- HailoRT installed and configured
- Appropriate VLM HEF file from Hailo Model Zoo GenAI
- Image files for testing (optional)

### Compilation
```bash
# From examples directory
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target vlm_example

# Or from vlm_example directory
cd genai/vlm_example
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```

### Execution
```bash
./build/cpp/vlm_example/vlm_example <hef_path>
```

## VLM Structured Prompts

### Multi-modal Message Format

VLM structured prompts support both text and image content within the same message:

```json
{
    "role": "user|system|assistant",
    "content": [
        {
            "type": "text",
            "text": "text content"
        },
        {
            "type": "image"
        }
    ]
}
```

**Content Types:**
- **`text`**: Text content with `text` field
- **`image`**: Image placeholder (actual image data provided separately)

### Key Differences from LLM

| Feature | LLM | VLM |
|---------|-----|-----|
| Content Structure | Single string | Array of content items |
| Image Support | ❌ | ✅ |
| Generate Method | `write()` + `generate()` | `generate(messages, frames)` |
| Image Validation | N/A | Automatic count verification |

## Implementation Examples

### Text-Only Conversation
```cpp
std::vector<std::string> messages = {
    R"({"role": "user", "content": [{"type": "text", "text": "Hello, how are you?"}]})"
};
std::vector<hailort::MemoryView> frames; // Empty for text-only

/*
    Demonstrating both generation flows, with or without generator instance.
    Both options are equivalent.
*/

// With simplified API (without generator)
auto completion = vlm.generate(messages, frames).expect("Failed to generate");

// With generator API
auto generator = vlm.create_generator().expect("Failed to create generator");
auto completion = generator.generate(messages, frames).expect("Failed to generate");
```

### Single Image with Text
```cpp
std::vector<std::string> messages = {
    R"({"role": "user", "content": [
        {"type": "image"},
        {"type": "text", "text": "Describe this image"}
    ]})"
};
std::vector<hailort::MemoryView> frames = {image_data};

// With simplified API (without generator)
auto completion = vlm.generate(messages, frames).expect("Failed to generate");

```

### Multiple Images
```cpp
std::vector<std::string> messages = {
    R"({"role": "user", "content": [
        {"type": "image"},
        {"type": "image"},
        {"type": "text", "text": "Compare these two images"}
    ]})"
};
std::vector<hailort::MemoryView> frames = {image1_data, image2_data};

// With simplified API (without generator)
auto completion = vlm.generate(messages, frames).expect("Failed to generate");
```

## Advanced Features

### Image Count Validation

The VLM API automatically validates that the number of provided image frames matches the number of image entries in the messages:

```cpp
// Valid: 2 images in message, 2 frames provided
std::vector<std::string> messages = {
    R"({"role": "user", "content": [
        {"type": "image"}, {"type": "image"},
        {"type": "text", "text": "Compare these"}
    ]})"
};
std::vector<hailort::MemoryView> frames = {img1, img2}; // 2 frames

// Invalid: 1 image in message, 0 frames provided - will cause error
std::vector<std::string> messages = {
    R"({"role": "user", "content": [
        {"type": "image"},
        {"type": "text", "text": "Compare these"}
    ]})"
};
std::vector<hailort::MemoryView> frames = {}; // 0 frames
```

### Benefits of Structured Prompts

1. **Standardization**: Consistent format across different models and applications
2. **Flexibility**: Easy role management (system, user, assistant)
3. **Template Integration**: Automatic prompt template application
4. **Multi-modal Support**: Seamless text and image content handling

## Best Practices

1. **Use structured prompts** for better organization and template integration
2. **Validate image counts** before calling generate()
3. **Handle errors gracefully** with proper exception handling
4. **Clear context periodically** for long conversations
5. **Follow JSON standards** for message formatting
6. **Use appropriate roles** (system, user, assistant)

For detailed code examples and advanced usage, see the HailoRT user guide and API documentation.