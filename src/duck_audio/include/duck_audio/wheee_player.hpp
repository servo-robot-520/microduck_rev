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
        /// 创建 wheee 的 StreamPublisher；真正启动在第一次 hold=true。
        WheeePlayer(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session);
        /// 停止 writer 并释放 session。
        ~WheeePlayer();

        WheeePlayer(const WheeePlayer&) = delete;
        WheeePlayer& operator=(const WheeePlayer&) = delete;

        /// 5–10 Hz 的按住电平；300 ms 没有新消息按松手处理。
        void on_hold(bool held);

        /// 50 Hz 调用：刷新 lease、回收已经落地的 writer。
        void tick();

        /// 节点退出时停止；会等待 writer 结束。
        void stop();

        /// writer 是否仍在运行（包括正在播放 end 段）。
        [[nodiscard]] bool active() const {
            return running_.load();
        }

    private:
        /// 同一 variant 的三段 PCM；由 writer 独占。
        struct Segments {
            WavPcm start;
            WavPcm loop;
            WavPcm end;
        };

        /// 从 bank/wheee 选择一个字母并读取 start/loop/end。
        [[nodiscard]] bool load_segments(Segments* segments, std::string* error) const;
        /// writer 主循环：start → 按住 loop → end。
        void run();
        /// 按 10 ms 节奏发布一段 PCM；running_=false 时提前退出。
        void publish_paced(const WavPcm& segment);

        rclcpp::Node& node_;
        VoiceRuntime& runtime_;
        AudioSession& session_;
        StreamPublisher publisher_;

        mutable std::mutex mutex_; ///< 保护 thread_ 生命周期与 last_hold_
        std::thread thread_;
        std::atomic<bool> running_{false}; ///< writer 循环条件
        std::atomic<bool> hold_{false}; ///< 当前 lease
        std::atomic<bool> finished_{false}; ///< writer 已发 eos，等待 tick 回收
        std::chrono::steady_clock::time_point last_hold_{}; ///< 最后一次 hold=true
        std::string last_error_; ///< 加载三段失败时的英文原因
    };

} // namespace duck_audio
