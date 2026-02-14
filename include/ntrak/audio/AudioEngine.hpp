#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace ntrak::audio {

/// Audio callback type: (output_buffer, frame_count) -> void
/// Buffer is interleaved stereo float samples (L, R, L, R, ...)
using AudioCallback = std::function<void(float* output, uint32_t frameCount)>;

class AudioEngine {
public:
    struct Impl;

    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void shutdown();

    /// Set a custom audio callback for generating samples
    void setAudioCallback(AudioCallback callback);

    /// Clear the audio callback (silence)
    void clearAudioCallback();

    /// Get the device sample rate
    uint32_t sampleRate() const;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace ntrak::audio
