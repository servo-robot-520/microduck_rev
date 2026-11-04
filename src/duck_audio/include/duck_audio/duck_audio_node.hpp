#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "duck_audio/audio_session.hpp"
#include "duck_audio/theremin_module.hpp"
#include "duck_audio/voice_module.hpp"
#include "duck_audio/voice_runtime.hpp"
#include "duck_audio/wheee_player.hpp"
#include "duck_audio_interface/msg/voice_state.hpp"
#include "duck_audio_interface/msg/wheee_hold.hpp"
#include "duck_audio_interface/srv/play_voice.hpp"
#include "duck_audio_interface/srv/set_theremin.hpp"

namespace duck_audio {

/// duck_audio 的组合式节点：内部持有调用方传入的 rclcpp::Node，不继承它。
///
/// 其他功能包可以把自己的节点传进来，共享同一个执行器：
/// @code
/// auto node = std::make_shared<rclcpp::Node>("duck_audio");
/// auto audio = std::make_shared<duck_audio::DuckAudioNode>(node);
/// @endcode
///
/// 合唱暂未实现；`SetChorale` 服务不会被广告。
class DuckAudioNode {
public:
    explicit DuckAudioNode(rclcpp::Node::SharedPtr node);
    ~DuckAudioNode();

    DuckAudioNode(const DuckAudioNode&) = delete;
    DuckAudioNode& operator=(const DuckAudioNode&) = delete;
    DuckAudioNode(DuckAudioNode&&) = delete;
    DuckAudioNode& operator=(DuckAudioNode&&) = delete;

    [[nodiscard]] const rclcpp::Node::SharedPtr& node() const noexcept { return node_; }

private:
    void tick();
    void publish_state();
    void handle_play_voice(std::shared_ptr<duck_audio_interface::srv::PlayVoice::Request> request,
                           std::shared_ptr<duck_audio_interface::srv::PlayVoice::Response> response);
    void handle_set_theremin(
            std::shared_ptr<duck_audio_interface::srv::SetTheremin::Request> request,
            std::shared_ptr<duck_audio_interface::srv::SetTheremin::Response> response);

    rclcpp::Node::SharedPtr node_;
    AudioSession session_;
    std::unique_ptr<VoiceRuntime> runtime_;
    std::unique_ptr<VoiceModule> voice_;
    std::unique_ptr<WheeePlayer> wheee_;
    std::unique_ptr<ThereminModule> theremin_;

    rclcpp::CallbackGroup::SharedPtr theremin_group_;
    rclcpp::Service<duck_audio_interface::srv::PlayVoice>::SharedPtr play_service_;
    rclcpp::Service<duck_audio_interface::srv::SetTheremin>::SharedPtr theremin_service_;
    rclcpp::Subscription<duck_audio_interface::msg::WheeeHold>::SharedPtr hold_subscription_;
    rclcpp::Publisher<duck_audio_interface::msg::VoiceState>::SharedPtr state_publisher_;
    rclcpp::TimerBase::SharedPtr tick_timer_;
    rclcpp::TimerBase::SharedPtr state_timer_;
};

} // namespace duck_audio
