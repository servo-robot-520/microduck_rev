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

    /// source_config 决定深度图/点云输入；rolloff_hz 传递给 Stream 的扬声器补偿。
    ThereminModule(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session,
                   GeminiHandSource::Config source_config,
                   rclcpp::CallbackGroup::SharedPtr callback_group, double rolloff_hz);
    ~ThereminModule();

    ThereminModule(const ThereminModule&) = delete;
    ThereminModule& operator=(const ThereminModule&) = delete;

    /// 启停乐器；启用前检查音库、深度新鲜度和 AudioSession 所有权。
    [[nodiscard]] Result enable(bool active);
    /// 是否正在演奏（writer 仍可能处于淡出阶段）。
    [[nodiscard]] bool active() const { return active_.load(); }
    /// 深度输入是否仍在更新。
    [[nodiscard]] bool healthy() const;

    /// 50 Hz：读 hand、更新 slew 参数、发布状态与嘴部目标。
    void tick();

    /// 停用并等待 writer 结束；节点析构时调用。
    void stop();

private:
    /// 100 Hz writer：读原子参数、调用 FFI、发 AudioStream。
    void run();
    /// 发布 ThereminState；diagnostic 是英文短状态（如 no depth frames）。
    void publish_state(std::optional<HandSample> hand, double hz, const std::string& diagnostic);
    /// 发布 0..1 的嘴部目标；无手时为 0。
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

    std::atomic<bool> active_{false};  ///< 服务启停状态
    std::atomic<bool> running_{false}; ///< writer 循环条件
    std::atomic<uint64_t> hz_bits_{0};    ///< double bits，避免锁
    std::atomic<uint64_t> level_bits_{0}; ///< double bits，0..1
    std::atomic<uint64_t> open_bits_{0};  ///< double bits，嘴/音色 0..1
    double last_hz_{0.0}; ///< 无手时保持最后音高
};

} // namespace duck_audio
