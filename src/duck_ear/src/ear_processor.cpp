#include "duck_ear/ear_processor.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace duck_ear {

EarProcessor::~EarProcessor() {
    if (ear_ != nullptr) {
        duck_ear_free(ear_);
        ear_ = nullptr;
    }
}

bool EarProcessor::initialize(const Config& config, std::string* error) {
    if (ear_ != nullptr) {
        return true;
    }
    if (config.model.empty()) {
        if (error != nullptr) {
            *error = "petting model path is empty";
        }
        return false;
    }
    char err[512]{};
    ear_ = duck_ear_new(config.model.string().c_str(), config.enter_threshold,
                        config.exit_threshold, config.stride, err, sizeof(err));
    if (ear_ == nullptr) {
        last_error_ = err;
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    return true;
}

std::vector<EarEvent> EarProcessor::push(const int16_t* samples, const std::size_t frames,
                                         const uint32_t sample_rate,
                                         const uint16_t channels) {
    if (ear_ == nullptr || samples == nullptr || frames == 0 || sample_rate == 0 ||
        channels == 0) {
        return {};
    }
    if (sample_rate != input_rate_ || channels != input_channels_) {
        resampler_ = std::make_unique<LinearResampler>(sample_rate, 16000);
        input_rate_ = sample_rate;
        input_channels_ = channels;
    }
    mono_.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        float sum = 0.0F;
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
            sum += static_cast<float>(samples[i * channels + channel]) / 32768.0F;
        }
        mono_[i] = sum / static_cast<float>(channels);
    }
    const auto resampled = resampler_->push(mono_.data(), mono_.size());
    if (resampled.empty()) {
        return {};
    }

    int32_t events[8]{};
    std::size_t count = 0;
    float probability = -1.0F;
    char err[512]{};
    const int status = duck_ear_push(ear_, resampled.data(), resampled.size(), events,
                                     std::size(events), &count, &probability, err, sizeof(err));
    if (status != DUCK_EAR_OK) {
        last_error_ = err;
        return {};
    }
    std::vector<EarEvent> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(EarEvent{
                events[i] == 0 ? EarEvent::Type::Start : EarEvent::Type::End, probability});
    }
    return out;
}

bool EarProcessor::is_petting() const {
    return ear_ != nullptr && duck_ear_is_petting(ear_) != 0;
}

} // namespace duck_ear
