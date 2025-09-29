# Speech2Text Example

This example demonstrates how to use the HailoRT GenAI Speech2Text API to transcribe audio files into text using a Whisper-based model running on Hailo hardware.

## Overview

The Speech2Text example application shows how to:

- Load a Speech2Text model (HEF file) onto a Hailo device.
- Accept user input for audio file path.
- Process the user's input for audio file, and displays the transcribed text segments with timestamps.

The example is implemented in C++ and uses the HailoRT GenAI C++ API.

## Prerequisites

- HailoRT installed and configured
- Appropriate Speech2Text HEF file from Hailo Model Zoo GenAI
- Audio file must be in format: PCM float32 normalized to [-1.0, 1.0), mono, little-endian, at 16 kHz.
    See https://github.com/openai/whisper/blob/c0d2f624c09dc18e709e37c2ad90c039a4eb72a2/whisper/audio.py#25

### Compilation
```bash
# From examples directory
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release --target speech2text_example

# Or from speech2text_example directory
cd genai/speech2text_example
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --config release
```

### Execution
```bash
./build/cpp/speech2text_example/speech2text_example <hef_path> <input_audio_file_path>
```