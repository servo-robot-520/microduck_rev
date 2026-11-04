#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include "duck_audio/hand_source.hpp"
#include "duck_audio/hand_tracker.hpp"

namespace duck_audio {

/// Orbbec Gemini 深度图/点云输入。点云可选 TF 到 target_frame；深度图始终按相机系使用。
class GeminiHandSource final : public HandSource {
public:
    struct Config {
        std::string input{"depth_image"}; ///< depth_image | point_cloud
        std::string depth_topic{"/camera/depth/image_raw"}; ///< 16UC1，毫米
        std::string cloud_topic{"/camera/depth/points"};     ///< XYZ，米
        std::string target_frame; ///< 空 = 相机光学系；点云可 TF 到此 frame
        HandConfig hand;          ///< ROI、hold、stale 等手部参数
    };

    /// 创建深度图或点云订阅；callback_group 与 50 Hz 控制定时器分开。
    GeminiHandSource(rclcpp::Node& node, Config config,
                     rclcpp::CallbackGroup::SharedPtr callback_group);

    /// 最近一帧的手；超过 stale 或无帧时返回 nullopt。
    [[nodiscard]] std::optional<HandSample> latest(
            std::chrono::steady_clock::time_point now) override;
    /// 最近是否有未超时的输入帧；不代表当前一定看得到手。
    [[nodiscard]] bool healthy(std::chrono::steady_clock::time_point now) const override;
    /// 清空 tracker 和最近帧；重新启用特雷门时调用。
    void reset() override;

private:
    /// 深度图回调：ROI 裁剪、毫米转米、交给 HandTracker。
    void on_depth(const sensor_msgs::msg::Image::ConstSharedPtr& message);
    /// 点云回调：可选 TF、XYZ ROI 过滤、交给 HandTracker。
    void on_cloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr& message);
    /// 记录本帧时间与结果；早期错误路径也调用它，保证 healthy 能反映“有帧”。
    void store(std::optional<HandSample> hand, std::chrono::steady_clock::time_point now);

    rclcpp::Node& node_;
    Config config_;
    HandTracker tracker_;

    std::unique_ptr<tf2_ros::Buffer> tf_buffer_;              ///< 仅点云 + target_frame 时创建
    std::unique_ptr<tf2_ros::TransformListener> tf_listener_; ///< 同上

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_subscription_;
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_subscription_;

    mutable std::mutex mutex_; ///< 保护 tracker_、last_hand_、last_frame_
    std::optional<HandSample> last_hand_;
    std::chrono::steady_clock::time_point last_frame_{}; ///< 本地接收时间，不用相机时钟
    bool warned_tf_{false}; ///< TF 失败只告警一次，避免 15 Hz 刷屏
};

} // namespace duck_audio
