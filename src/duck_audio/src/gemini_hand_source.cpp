#include "duck_audio/gemini_hand_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>
#include <tf2/exceptions.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.hpp>

namespace duck_audio {

GeminiHandSource::GeminiHandSource(rclcpp::Node& node, Config config,
                                   rclcpp::CallbackGroup::SharedPtr callback_group)
    : node_(node), config_(std::move(config)), tracker_(config_.hand) {
    if (!config_.target_frame.empty()) {
        tf_buffer_ = std::make_unique<tf2_ros::Buffer>(node_.get_clock());
        tf_listener_ = std::make_unique<tf2_ros::TransformListener>(*tf_buffer_);
    }
    const auto qos = rclcpp::SensorDataQoS().keep_last(1);
    rclcpp::SubscriptionOptions options;
    options.callback_group = std::move(callback_group);
    if (config_.input == "point_cloud") {
        cloud_subscription_ = node_.create_subscription<sensor_msgs::msg::PointCloud2>(
                config_.cloud_topic, qos,
                [this](const sensor_msgs::msg::PointCloud2::ConstSharedPtr& message) {
                    on_cloud(message);
                },
                options);
        RCLCPP_INFO(node_.get_logger(), "theremin input: point cloud %s", config_.cloud_topic.c_str());
    } else {
        depth_subscription_ = node_.create_subscription<sensor_msgs::msg::Image>(
                config_.depth_topic, qos,
                [this](const sensor_msgs::msg::Image::ConstSharedPtr& message) {
                    on_depth(message);
                },
                options);
        RCLCPP_INFO(node_.get_logger(), "theremin input: depth image %s", config_.depth_topic.c_str());
    }
}

std::optional<HandSample> GeminiHandSource::latest(
        const std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock(mutex_);
    if (last_frame_ == std::chrono::steady_clock::time_point{}) {
        return std::nullopt;
    }
    if (now - last_frame_ > config_.hand.stale) {
        return std::nullopt;
    }
    return last_hand_;
}

bool GeminiHandSource::healthy(const std::chrono::steady_clock::time_point now) const {
    std::scoped_lock lock(mutex_);
    return last_frame_ != std::chrono::steady_clock::time_point{} &&
           now - last_frame_ <= config_.hand.stale;
}

void GeminiHandSource::reset() {
    std::scoped_lock lock(mutex_);
    tracker_.reset();
    last_hand_.reset();
    last_frame_ = {};
}

void GeminiHandSource::store(std::optional<HandSample> hand,
                             const std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock(mutex_);
    last_frame_ = now;
    last_hand_ = hand;
}

void GeminiHandSource::on_depth(const sensor_msgs::msg::Image::ConstSharedPtr& message) {
    if (message->encoding != sensor_msgs::image_encodings::TYPE_16UC1 ||
        message->step < message->width * sizeof(uint16_t) || message->width == 0 ||
        message->height == 0 ||
        message->data.size() < static_cast<std::size_t>(message->step) * message->height) {
        store(std::nullopt, std::chrono::steady_clock::now());
        return;
    }
    const int x0 = static_cast<int>(static_cast<double>(message->width) *
                                   (1.0 - config_.hand.roi_x_frac) / 2.0);
    const int x1 = static_cast<int>(message->width) - x0;
    const int y0 = static_cast<int>(static_cast<double>(message->height) * config_.hand.roi_y0_frac);
    const int y1 = static_cast<int>(static_cast<double>(message->height) * config_.hand.roi_y1_frac);

    std::vector<float> depths;
    depths.reserve(static_cast<std::size_t>(std::max(0, x1 - x0) * std::max(0, y1 - y0)));
    for (int v = std::max(0, y0); v < std::min(static_cast<int>(message->height), y1); ++v) {
        const auto* row = message->data.data() + static_cast<std::size_t>(v) * message->step;
        for (int u = std::max(0, x0); u < std::min(static_cast<int>(message->width), x1); ++u) {
            uint16_t millimetres = 0;
            std::memcpy(&millimetres, row + static_cast<std::size_t>(u) * sizeof(uint16_t),
                        sizeof(millimetres));
            if (millimetres == 0) {
                continue;
            }
            const double metres = static_cast<double>(millimetres) * 0.001;
            if (metres >= config_.hand.near_m && metres <= config_.hand.far_m) {
                depths.push_back(static_cast<float>(metres));
            }
        }
    }
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(mutex_);
        last_frame_ = now;
        last_hand_ = tracker_.update(depths, now);
    }
}

void GeminiHandSource::on_cloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr& message) {
    if (message->point_step < 12 || message->width == 0 || message->height == 0 ||
        message->data.size() <
                static_cast<std::size_t>(message->row_step) * message->height) {
        store(std::nullopt, std::chrono::steady_clock::now());
        return;
    }

    sensor_msgs::msg::PointCloud2 transformed;
    const sensor_msgs::msg::PointCloud2* cloud = message.get();
    if (!config_.target_frame.empty() && !message->header.frame_id.empty()) {
        try {
            const auto transform = tf_buffer_->lookupTransform(
                    config_.target_frame, message->header.frame_id, message->header.stamp,
                    rclcpp::Duration::from_seconds(0.05));
            tf2::doTransform(*message, transformed, transform);
            cloud = &transformed;
        } catch (const tf2::TransformException& exception) {
            if (!warned_tf_) {
                warned_tf_ = true;
                RCLCPP_WARN(node_.get_logger(), "theremin TF unavailable: %s", exception.what());
            }
            store(std::nullopt, std::chrono::steady_clock::now());
            return;
        }
    }

    std::vector<float> depths;
    depths.reserve(std::min<std::size_t>(cloud->width * cloud->height, 200000));
    sensor_msgs::PointCloud2ConstIterator<float> x(*cloud, "x");
    sensor_msgs::PointCloud2ConstIterator<float> y(*cloud, "y");
    sensor_msgs::PointCloud2ConstIterator<float> z(*cloud, "z");
    for (; x != x.end(); ++x, ++y, ++z) {
        if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
            continue;
        }
        if (*z < config_.hand.near_m || *z > config_.hand.far_m ||
            std::abs(*x) > config_.hand.lateral_m || *y < config_.hand.y_min_m ||
            *y > config_.hand.y_max_m) {
            continue;
        }
        depths.push_back(*z);
    }
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock(mutex_);
        last_frame_ = now;
        last_hand_ = tracker_.update(depths, now);
    }
}

} // namespace duck_audio
