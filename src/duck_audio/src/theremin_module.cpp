#include "duck_audio/theremin_module.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

#include "duck_audio/ffi/microduck_sounds.h"

namespace duck_audio {
namespace {

// 10 ms 一块：audio_io 的 stream timeout 是 200 ms，留足调度余量。
constexpr std::size_t kChunkFrames = 480; // 10 ms @ 48 kHz
constexpr auto kChunkPeriod = std::chrono::milliseconds(10);

// 用 u64 bits 传 double，避免 std::atomic<double> 在部分 ARM 上不是 lock-free。
uint64_t bits(const double value) {
    uint64_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

double from_bits(const uint64_t value) {
    double result = 0.0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

} // namespace

ThereminModule::ThereminModule(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session,
                               GeminiHandSource::Config source_config,
                               rclcpp::CallbackGroup::SharedPtr callback_group,
                               const double rolloff_hz)
    : node_(node),
      runtime_(runtime),
      session_(session),
      input_(source_config.input),
      source_(node, std::move(source_config), std::move(callback_group)),
      publisher_(node, "theremin"),
      rolloff_hz_(rolloff_hz) {
    // 状态用 transient_local；嘴部目标用普通 reliable 小消息。
    state_publisher_ = node_.create_publisher<duck_audio_interface::msg::ThereminState>(
            "~/theremin/state", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());
    mouth_publisher_ = node_.create_publisher<std_msgs::msg::Float64>(
            "~/mouth", rclcpp::QoS(rclcpp::KeepLast(1)).reliable());
    hz_bits_ = bits(runtime_.has_voice() ? duck_hz_at(runtime_.voice(), 0.5) : 0.0);
    last_hz_ = from_bits(hz_bits_);
}

ThereminModule::~ThereminModule() {
    stop();
}

ThereminModule::Result ThereminModule::enable(const bool desired) {
    // 停用：把 level/open 目标清零，让 writer 用 Stream 的 slew 淡出，再 join。
    if (!desired) {
        {
            std::scoped_lock lock(mutex_);
            if (!active_.load()) {
                return Result{true, {}};
            }
            active_ = false;
            level_bits_ = bits(0.0);
            open_bits_ = bits(0.0);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        {
            std::scoped_lock lock(mutex_);
            if (stream_ != nullptr) {
                duck_stream_free(stream_);
                stream_ = nullptr;
            }
            running_ = false;
        }
        session_.release(AudioOwner::Theremin);
        publish_mouth(0.0);
        publish_state(std::nullopt, last_hz_, "disabled");
        return Result{true, {}};
    }

    // 启用前必须同时满足：有音库、深度帧新鲜、没有其他实时流占用。
    std::scoped_lock lock(mutex_);
    if (active_.load()) {
        return Result{true, {}};
    }
    if (!runtime_.has_voice()) {
        return Result{false, "this robot has no voice bank"};
    }
    if (!source_.healthy(std::chrono::steady_clock::now())) {
        return Result{false, "no fresh depth frames"};
    }
    if (!session_.try_acquire(AudioOwner::Theremin)) {
        return Result{false, "another live voice stream owns the audio channel"};
    }
    // 特雷门复用 wheee 的软化音色；variant 固定为 0，保证每次拿起是同一件乐器。
    DuckStream* stream = duck_stream_new_wheee(runtime_.voice(), 0);
    if (stream == nullptr) {
        session_.release(AudioOwner::Theremin);
        return Result{false, "cannot create the theremin stream"};
    }
    duck_stream_set_speaker_rolloff(stream, rolloff_hz_);
    level_bits_ = bits(0.0);
    open_bits_ = bits(0.0);
    stream_ = stream;
    active_ = true;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    publish_state(std::nullopt, last_hz_, "enabled");
    return Result{true, {}};
}

bool ThereminModule::healthy() const {
    return source_.healthy(std::chrono::steady_clock::now());
}

void ThereminModule::tick() {
    // 50 Hz 只写目标值；真正的 100 Hz 合成在 run()。
    if (!active_.load()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool sensor_ok = source_.healthy(now);
    const auto hand = sensor_ok ? source_.latest(now) : std::nullopt;
    if (hand.has_value()) {
        // closeness 是一个手势的三个输出：音高、嘴、电平。
        last_hz_ = duck_hz_at(runtime_.voice(), hand->closeness);
        hz_bits_ = bits(last_hz_);
        level_bits_ = bits(1.0);
        open_bits_ = bits(hand->closeness);
        publish_mouth(hand->closeness);
        publish_state(hand, last_hz_, hand->held ? "held" : "ok");
    } else {
        hz_bits_ = bits(last_hz_);
        level_bits_ = bits(0.0);
        open_bits_ = bits(0.0);
        publish_mouth(0.0);
        publish_state(std::nullopt, last_hz_, sensor_ok ? "no hand" : "no depth frames");
    }
}

void ThereminModule::stop() {
    if (active_.load()) {
        (void)enable(false);
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::scoped_lock lock(mutex_);
    if (stream_ != nullptr) {
        duck_stream_free(stream_);
        stream_ = nullptr;
    }
    session_.release(AudioOwner::Theremin);
}

void ThereminModule::run() {
    // writer 独占 stream_ 的 set/block；停用时靠 active=false + is_silent 退出。
    std::array<float, kChunkFrames> block{};
    std::array<int16_t, kChunkFrames> pcm{};
    auto deadline = std::chrono::steady_clock::now();
    while (running_.load()) {
        DuckStream* stream = nullptr;
        {
            std::scoped_lock lock(mutex_);
            stream = stream_;
        }
        if (stream == nullptr) {
            break;
        }
        const double hz = from_bits(hz_bits_.load());
        const double level = from_bits(level_bits_.load());
        const double open = from_bits(open_bits_.load());
        if (duck_stream_set(stream, hz, level, open) != DUCK_OK ||
            duck_stream_block(stream, block.data(), block.size()) != DUCK_OK) {
            break;
        }
        for (std::size_t i = 0; i < block.size(); ++i) {
            const float clamped = std::clamp(block[i], -1.0F, 1.0F);
            pcm[i] = static_cast<int16_t>(std::lround(clamped * 32767.0F));
        }
        publisher_.publish(pcm.data(), pcm.size(), false);
        // 目标 level 已为 0 且 slew 走到静音后再退出，避免硬切。
        if (!active_.load() && duck_stream_is_silent(stream) != 0) {
            break;
        }
        deadline += kChunkPeriod;
        std::this_thread::sleep_until(deadline);
    }
    // 结束时发 eos，让 audio_io 立即释放该 stream，而不是等 200 ms timeout。
    publisher_.publish_eos();
}

void ThereminModule::publish_state(const std::optional<HandSample> hand, const double hz,
                                   const std::string& diagnostic) {
    duck_audio_interface::msg::ThereminState state;
    state.header.stamp = node_.now();
    state.header.frame_id = "duck_audio";
    state.active = active_.load();
    state.sensor_ok = source_.healthy(std::chrono::steady_clock::now());
    state.input = input_;
    state.range_m = hand.has_value() ? static_cast<float>(hand->range_m) : 0.0F;
    state.closeness = hand.has_value() ? static_cast<float>(hand->closeness) : 0.0F;
    state.note_hz = static_cast<float>(hz);
    state.mouth = hand.has_value() ? static_cast<float>(hand->closeness) : 0.0F;
    state.points = hand.has_value() ? static_cast<uint32_t>(hand->points) : 0;
    state.held = hand.has_value() && hand->held;
    state.diagnostic = diagnostic;
    state_publisher_->publish(state);
}

void ThereminModule::publish_mouth(const double open) {
    std_msgs::msg::Float64 message;
    message.data = open;
    mouth_publisher_->publish(message);
}

} // namespace duck_audio
