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
    /// 在节点上创建 /audio_io/stream publisher，并固定 stream_id。
    StreamPublisher(rclcpp::Node& node, std::string stream_id);

    /// 发布一块 S16LE PCM；frames 是样本数，不是字节数。
    void publish(const int16_t* samples, std::size_t frames, bool eos = false);
    /// 只发 eos=true 的空包，让 audio_io 立即结束该流。
    void publish_eos();

private:
    /// 统一构造 AudioStream 消息、推进 sequence 并发布。
    void publish_impl(const int16_t* samples, std::size_t frames, bool eos);

    rclcpp::Node& node_;
    std::string stream_id_;
    rclcpp::Publisher<audio_io_interface::msg::AudioStream>::SharedPtr publisher_;
    std::mutex mutex_;        ///< 保护 sequence_，writer 线程是唯一调用者
    uint64_t sequence_{0};   ///< 从 0 递增；audio_io 用来发现丢包/乱序
};

} // namespace duck_audio
