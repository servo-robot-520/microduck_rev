#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "duck_audio/ffi/microduck_sounds.h"

namespace duck_audio {

/// 音库发现、seed 解析和 DuckVoice 句柄的生命周期。
class VoiceRuntime {
public:
    struct Config {
        std::string bank;
        uint32_t seed_override{0};
    };

    VoiceRuntime(Config config, rclcpp::Logger logger);
    ~VoiceRuntime();

    VoiceRuntime(const VoiceRuntime&) = delete;
    VoiceRuntime& operator=(const VoiceRuntime&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool has_voice() const { return has_voice_; }
    [[nodiscard]] uint32_t seed() const { return seed_; }
    [[nodiscard]] uint32_t bank_version() const { return bank_version_; }
    [[nodiscard]] const std::string& bank() const { return config_.bank; }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }
    [[nodiscard]] const DuckVoice* voice() const { return voice_; }
    [[nodiscard]] std::optional<DuckPersonality> personality() const;

private:
    [[nodiscard]] bool scan_bank();
    [[nodiscard]] bool read_marker(uint32_t* seed, uint32_t* version) const;

    Config config_;
    rclcpp::Logger logger_;
    DuckVoice* voice_{nullptr};
    bool has_voice_{false};
    uint32_t seed_{0};
    uint32_t bank_version_{0};
    std::string last_error_;
};

} // namespace duck_audio
