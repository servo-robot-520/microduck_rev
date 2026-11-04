#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>

#include "duck_audio/audio_session.hpp"
#include "duck_audio/gemini_hand_source.hpp"
#include "duck_audio/stream_publisher.hpp"
#include "duck_audio/voice_runtime.hpp"
#include "duck_audio_interface/msg/theremin_state.hpp"

namespace duck_audio {

/// 特雷门：Gemini 手距 → Rust Stream::wheee → AudioStream；嘴部同步输出。
class ThereminModule {
public:
    struct Result {
        bool accepted{false};
        std::string reason;
    };

    ThereminModule(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session,
                   GeminiHandSource::Config source_config,
                   rclcpp::CallbackGroup::SharedPtr callback_group, double rolloff_hz);
    ~ThereminModule();

    ThereminModule(const ThereminModule&) = delete;
    ThereminModule& operator=(const ThereminModule&) = delete;

    [[nodiscard]] Result enable(bool active);
    [[nodiscard]] bool active() const { return active_.load(); }
    [[nodiscard]] bool healthy() const;

    /// 50 Hz：读 hand、更新参数、发布状态与嘴部目标。
    void tick();

    void stop();

private:
    void run();
    void publish_state(std::optional<HandSample> hand, double hz, const std::string& diagnostic);
    void publish_mouth(double open);

    rclcpp::Node& node_;
    VoiceRuntime& runtime_;
    AudioSession& session_;
    std::string input_;
    GeminiHandSource source_;
    StreamPublisher publisher_;

    rclcpp::Publisher<duck_audio_interface::msg::ThereminState>::SharedPtr state_publisher_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr mouth_publisher_;

    mutable std::mutex mutex_;
    std::thread thread_;
    DuckStream* stream_{nullptr};
    double rolloff_hz_{300.0};

    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> hz_bits_{0};
    std::atomic<uint64_t> level_bits_{0};
    std::atomic<uint64_t> open_bits_{0};
    double last_hz_{0.0};
};

} // namespace duck_audio
