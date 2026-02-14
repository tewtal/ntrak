#include "ntrak/audio/AudioEngine.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace ntrak::audio {

struct AudioEngine::Impl {
    ma_device device{};
    uint32_t sample_rate = 48000;
    bool initialized = false;

    // Custom callback support
    std::mutex callback_mutex;
    AudioCallback audio_callback;
};

static void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* impl = static_cast<AudioEngine::Impl*>(device->pUserData);
    if (!impl) {
        return;
    }
    auto* out = static_cast<float*>(output);

    // Check for custom callback first
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        if (impl->audio_callback) {
            impl->audio_callback(out, frame_count);
            return;
        }
    }

    // No custom callback — fill silence
    std::memset(out, 0, frame_count * 2 * sizeof(float));
}

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.dataCallback = dataCallback;
    config.pUserData = impl_.get();

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        return false;
    }

    impl_->initialized = true;

    impl_->sample_rate = impl_->device.sampleRate;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
        return false;
    }

    return true;
}

void AudioEngine::shutdown() {
    if (impl_ && impl_->initialized) {
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
    }
}

void AudioEngine::setAudioCallback(AudioCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->audio_callback = std::move(callback);
}

void AudioEngine::clearAudioCallback() {
    std::lock_guard<std::mutex> lock(impl_->callback_mutex);
    impl_->audio_callback = nullptr;
}

uint32_t AudioEngine::sampleRate() const {
    return impl_->sample_rate;
}

}  // namespace ntrak::audio
