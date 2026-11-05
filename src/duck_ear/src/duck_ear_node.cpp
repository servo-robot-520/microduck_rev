#include "duck_ear/duck_ear_node.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "audio_io_interface/ros_msg.hpp"
#include "duck_audio_interface/ros_msg.hpp"

namespace duck_ear {
    namespace {

        template <typename T>
        T parameter(const rclcpp::Node::SharedPtr& node, const std::string& name, const T& fallback) {
            // 组合式节点可能已由调用方声明过参数；先检查避免重复声明。
            if (!node->has_parameter(name)) {
                node->declare_parameter<T>(name, fallback);
            }
            return node->get_parameter(name).get_value<T>();
        }

        std::string default_model_path(const rclcpp::Node::SharedPtr& node) {
            try {
                return ament_index_cpp::get_package_share_directory("duck_ear") +
                    "/models/pet_detect.onnx";
            }
            catch (const std::exception& exception) {
                RCLCPP_WARN(node->get_logger(), "cannot locate duck_ear share directory: %s",
                            exception.what());
                return {};
            }
        }

    } // namespace

    DuckEarNode::DuckEarNode(rclcpp::Node::SharedPtr node) :
        node_(std::move(node)) {
        if (node_ == nullptr) {
            throw std::invalid_argument("DuckEarNode requires a non-null node");
        }

        auto model = parameter<std::string>(node_, "model", "");
        if (model.empty()) {
            model = default_model_path(node_);
        }
        EarProcessor::Config config;
        config.model = model;
        config.enter_threshold = parameter<float>(node_, "enter_threshold", 0.95);
        config.exit_threshold = parameter<float>(node_, "exit_threshold", 0.85);
        config.stride = static_cast<uint32_t>(parameter<int>(node_, "stride", 0));
        std::string error;
        if (!processor_.initialize(config, &error)) {
            RCLCPP_ERROR(node_->get_logger(), "petting detector unavailable: %s", error.c_str());
            return;
        }

        stream_id_ = parameter<std::string>(node_, "stream_id", "mic");
        publisher_ = node_->create_publisher<duck_audio_interface::msg::PettingEvent>(
            std::string(duck_audio_interface::kPrivatePettingEvent), rclcpp::QoS(rclcpp::KeepLast(10)).reliable());
        subscription_ = node_->create_subscription<audio_io_interface::msg::AudioStream>(
            audio_io_interface::topic_capture(audio_io_interface::kDefaultNodeFqn, node_->get_namespace()),
            rclcpp::QoS(rclcpp::KeepLast(2)).best_effort(),
            [this](const audio_io_interface::msg::AudioStream::ConstSharedPtr& message) {
                on_stream(message);
            });
        RCLCPP_INFO(node_->get_logger(), "duck_ear listening on %s", stream_id_.c_str());
    }

    void DuckEarNode::on_stream(
        const audio_io_interface::msg::AudioStream::ConstSharedPtr& message) {
        if (message->encoding != "s16le" || message->channels == 0 || message->channels > 2 ||
            message->sample_rate == 0 || message->data.size() % sizeof(int16_t) != 0 ||
            message->data.size() % (sizeof(int16_t) * message->channels) != 0) {
            if (!warned_decode_) {
                warned_decode_ = true;
                RCLCPP_WARN(node_->get_logger(), "unsupported capture stream: encoding=%s channels=%u",
                            message->encoding.c_str(), message->channels);
            }
            return;
        }
        if (!stream_id_.empty() && message->stream_id != stream_id_) {
            return;
        }
        const std::size_t frames = message->data.size() / (sizeof(int16_t) * message->channels);
        std::vector<int16_t> pcm(message->data.size() / sizeof(int16_t));
        std::memcpy(pcm.data(), message->data.data(), message->data.size());
        for (const auto& [type, confidence] : processor_.push(pcm.data(), frames, message->sample_rate,
                                                              message->channels)) {
            publish_event(type, confidence);
        }
    }

    void DuckEarNode::publish_event(const EarEvent::Type type, const float confidence) const {
        if (publisher_ == nullptr) {
            return;
        }
        duck_audio_interface::msg::PettingEvent message;
        message.header.stamp = node_->now();
        message.header.frame_id = "duck_ear";
        message.event = type == EarEvent::Type::Start
            ? duck_audio_interface::msg::PettingEvent::START
            : duck_audio_interface::msg::PettingEvent::END;
        message.confidence = confidence;
        publisher_->publish(message);
        RCLCPP_INFO(node_->get_logger(), "petting %s confidence %.2f",
                    type == EarEvent::Type::Start ? "start" : "end", confidence);
    }

} // namespace duck_ear
