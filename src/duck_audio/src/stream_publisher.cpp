#include "duck_audio/stream_publisher.hpp"

#include <cstring>

#include "audio_io_interface/ros_msg.hpp"

namespace duck_audio {

StreamPublisher::StreamPublisher(rclcpp::Node& node, std::string stream_id)
    : node_(node), stream_id_(std::move(stream_id)) {
    // 与 audio_io 的 ~/stream 订阅匹配：best_effort、只保留最新两块。
    const auto qos = rclcpp::QoS(rclcpp::KeepLast(2)).best_effort();
    publisher_ = node_.create_publisher<audio_io_interface::msg::AudioStream>(
            audio_io_interface::topic_stream(), qos);
}

void StreamPublisher::publish(const int16_t* samples, const std::size_t frames, const bool eos) {
    publish_impl(samples, frames, eos);
}

void StreamPublisher::publish_eos() {
    publish_impl(nullptr, 0, true);
}

void StreamPublisher::publish_impl(const int16_t* samples, const std::size_t frames,
                                   const bool eos) {
    audio_io_interface::msg::AudioStream message;
    message.header.stamp = node_.now();
    message.header.frame_id = "duck_audio";
    message.stream_id = stream_id_;
    // 固定格式：AudioStream 的消费端会按这个格式重采样/下混。
    message.encoding = "s16le";
    message.sample_rate = 48000;
    message.channels = 1;
    message.eos = eos;
    message.data.resize(frames * sizeof(int16_t));
    if (samples != nullptr && frames != 0) {
        std::memcpy(message.data.data(), samples, message.data.size());
    }
    {
        // sequence 递增，audio_io 可发现丢包/乱序；每个消息块都要有新序号。
        std::scoped_lock lock(mutex_);
        message.sequence = sequence_++;
    }
    publisher_->publish(message);
}

} // namespace duck_audio
