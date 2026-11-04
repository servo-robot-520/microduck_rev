#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "audio_io_interface/msg/audio_stream.hpp"

namespace duck_audio {

/// 向 /audio_io/stream 发布固定格式的 PCM：48 kHz、mono、s16le、10 ms 一块。
class StreamPublisher {
public:
    StreamPublisher(rclcpp::Node& node, std::string stream_id);

    void publish(const int16_t* samples, std::size_t frames, bool eos = false);
    void publish_eos();

private:
    void publish_impl(const int16_t* samples, std::size_t frames, bool eos);

    rclcpp::Node& node_;
    std::string stream_id_;
    rclcpp::Publisher<audio_io_interface::msg::AudioStream>::SharedPtr publisher_;
    std::mutex mutex_;
    uint64_t sequence_{0};
};

} // namespace duck_audio
