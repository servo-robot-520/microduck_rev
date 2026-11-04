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
        std::string input{"depth_image"}; // depth_image | point_cloud
        std::string depth_topic{"/camera/depth/image_raw"};
        std::string cloud_topic{"/camera/depth/points"};
        std::string target_frame; // 空 = 相机光学系
        HandConfig hand;
    };

    GeminiHandSource(rclcpp::Node& node, Config config,
                     rclcpp::CallbackGroup::SharedPtr callback_group);

    [[nodiscard]] std::optional<HandSample> latest(
            std::chrono::steady_clock::time_point now) override;
    [[nodiscard]] bool healthy(std::chrono::steady_clock::time_point now) const override;
    void reset() override;

private:
    void on_depth(const sensor_msgs::msg::Image::ConstSharedPtr& message);
    void on_cloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr& message);
    void store(std::optional<HandSample> hand, std::chrono::steady_clock::time_point now);

    rclcpp::Node& node_;
    Config config_;
    HandTracker tracker_;

    std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
    std::unique_ptr<tf2_ros::TransformListener> tf_listener_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_subscription_;
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_subscription_;

    mutable std::mutex mutex_;
    std::optional<HandSample> last_hand_;
    std::chrono::steady_clock::time_point last_frame_{};
    bool warned_tf_{false};
};

} // namespace duck_audio
