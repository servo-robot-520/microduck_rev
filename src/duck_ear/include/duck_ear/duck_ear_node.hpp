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
    /// 在传入节点上声明参数、加载模型、创建订阅和发布者。
    /// 模型/ONNX Runtime 不可用时只记录错误，节点仍可被 spin。
    explicit DuckEarNode(rclcpp::Node::SharedPtr node);
    ~DuckEarNode() = default;

    DuckEarNode(const DuckEarNode&) = delete;
    DuckEarNode& operator=(const DuckEarNode&) = delete;

    /// 调用方用于 add_node/spin 的节点句柄。
    [[nodiscard]] const rclcpp::Node::SharedPtr& node() const noexcept { return node_; }

private:
    /// 处理一块 AudioStream：校验格式、转 int16、重采样并调用分类器。
    void on_stream(const audio_io_interface::msg::AudioStream::ConstSharedPtr& message);
    /// 发布 PettingEvent；Start/End 只表示状态变化，不在这里执行动作。
    void publish_event(EarEvent::Type type, float confidence) const;

    rclcpp::Node::SharedPtr node_;
    EarProcessor processor_; ///< 重采样 + pet-detect 封装；只由订阅回调调用
    rclcpp::Subscription<audio_io_interface::msg::AudioStream>::SharedPtr subscription_;
    rclcpp::Publisher<duck_audio_interface::msg::PettingEvent>::SharedPtr publisher_{};
    std::string stream_id_;
    bool warned_decode_{false}; ///< 非法格式只告警一次，避免刷屏
};

} // namespace duck_ear
