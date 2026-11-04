#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "duck_audio/audio_session.hpp"
#include "duck_audio/stream_publisher.hpp"
#include "duck_audio/voice_runtime.hpp"
#include "duck_audio/wav_pcm_reader.hpp"

namespace duck_audio {

/// wheee：start → loop（按住）→ end，作为一条 AudioStream 发布。
class WheeePlayer {
public:
    WheeePlayer(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session);
    ~WheeePlayer();

    WheeePlayer(const WheeePlayer&) = delete;
    WheeePlayer& operator=(const WheeePlayer&) = delete;

    /// 5–10 Hz 的按住电平；300 ms 没有新消息按松手处理。
    void on_hold(bool held);

    /// 50 Hz 调用：刷新 lease、回收已经落地的 writer。
    void tick();

    /// 节点退出时停止；会等待 writer 结束。
    void stop();

    [[nodiscard]] bool active() const { return running_.load(); }

private:
    struct Segments {
        WavPcm start;
        WavPcm loop;
        WavPcm end;
    };

    [[nodiscard]] bool load_segments(Segments* segments, std::string* error) const;
    void run();
    void publish_paced(const WavPcm& segment);

    rclcpp::Node& node_;
    VoiceRuntime& runtime_;
    AudioSession& session_;
    StreamPublisher publisher_;

    mutable std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> hold_{false};
    std::atomic<bool> finished_{false};
    std::chrono::steady_clock::time_point last_hold_{};
    std::string last_error_;
};

} // namespace duck_audio
