#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "audio_io_interface/msg/audio_clip.hpp"
#include "duck_audio/audio_session.hpp"
#include "duck_audio/voice_runtime.hpp"

namespace duck_audio {

/// 一次性语义声音：选 WAV、发 AudioClip。实时流（wheee/特雷门）不在这里合成。
class VoiceModule {
public:
    struct PlayResult {
        bool accepted{false};
        std::string request_id;
        std::string reason;
    };

    /// publisher 在构造时创建；音库由 VoiceRuntime 持有。
    VoiceModule(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session);

    /// 播放一个一次性 tag；variant=0xFFFFFFFF 时随机选 WAV。
    [[nodiscard]] PlayResult play(const std::string& tag, uint32_t variant);
    /// 最近一次被接受的一次性 tag；用于 VoiceState。
    [[nodiscard]] std::string last_tag() const;

private:
    /// 统一构造拒绝结果。
    [[nodiscard]] PlayResult reject(std::string reason) const;
    /// 在 <bank>/<tag>/ 下选 WAV；失败写英文 error。
    [[nodiscard]] std::string choose_wav(const std::string& tag, uint32_t variant,
                                         std::string* error) const;

    rclcpp::Node& node_;
    VoiceRuntime& runtime_;
    AudioSession& session_;
    rclcpp::Publisher<audio_io_interface::msg::AudioClip>::SharedPtr publisher_;
    std::atomic<uint64_t> counter_{0}; ///< request_id 后缀
    mutable std::mutex tag_mutex_;     ///< 保护 last_tag_
    std::string last_tag_;
};

} // namespace duck_audio
