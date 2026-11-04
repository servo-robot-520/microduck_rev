#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "audio_io_interface/msg/audio_stream.hpp"
#include "duck_audio_interface/msg/petting_event.hpp"
#include "duck_ear/ear_processor.hpp"

namespace duck_ear {

/// duck_ear 的组合式节点：持有调用方传入的 rclcpp::Node，不继承它。
///
/// 订阅 /audio_io/capture，重采样到 16 kHz，调用 pet-detect，发布 ~/petting。
/// 动作不在本节点执行；行为/控制节点订阅事件后自行处理。
class DuckEarNode {
public:
    explicit DuckEarNode(rclcpp::Node::SharedPtr node);
    ~DuckEarNode() = default;

    DuckEarNode(const DuckEarNode&) = delete;
    DuckEarNode& operator=(const DuckEarNode&) = delete;

    [[nodiscard]] const rclcpp::Node::SharedPtr& node() const noexcept { return node_; }

private:
    void on_stream(const audio_io_interface::msg::AudioStream::ConstSharedPtr& message);
    void publish_event(EarEvent::Type type, float confidence);

    rclcpp::Node::SharedPtr node_;
    EarProcessor processor_;
    rclcpp::Subscription<audio_io_interface::msg::AudioStream>::SharedPtr subscription_;
    rclcpp::Publisher<duck_audio_interface::msg::PettingEvent>::SharedPtr publisher_;
    std::string stream_id_;
    bool warned_decode_{false};
};

} // namespace duck_ear
