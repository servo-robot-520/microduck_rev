#include "duck_audio/wheee_player.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <vector>

namespace duck_audio {
namespace {

constexpr std::size_t kChunkFrames = 480; // 10 ms @ 48 kHz
constexpr auto kChunkPeriod = std::chrono::milliseconds(10);
constexpr auto kHoldTimeout = std::chrono::milliseconds(300);

} // namespace

WheeePlayer::WheeePlayer(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session)
    : node_(node), runtime_(runtime), session_(session), publisher_(node, "wheee") {}

WheeePlayer::~WheeePlayer() {
    stop();
}

void WheeePlayer::on_hold(const bool held) {
    // 特雷门优先：乐器在手里时忽略扳机；否则 held=true 启动/刷新 lease。
    std::scoped_lock lock(mutex_);
    if (!runtime_.has_voice()) {
        return;
    }
    if (session_.owner() == AudioOwner::Theremin) {
        return;
    }
    if (!held) {
        hold_ = false;
        return;
    }
    last_hold_ = std::chrono::steady_clock::now();
    if (running_.load()) {
        hold_ = true;
        return;
    }
    if (!session_.try_acquire(AudioOwner::Wheee)) {
        return;
    }
    Segments segments;
    std::string error;
    if (!load_segments(&segments, &error)) {
        last_error_ = std::move(error);
        session_.release(AudioOwner::Wheee);
        RCLCPP_WARN(node_.get_logger(), "%s", last_error_.c_str());
        return;
    }
    finished_ = false;
    hold_ = true;
    running_ = true;
    // 三段音频 move 进 writer；之后 player 不再访问它们，避免数据竞争。
    thread_ = std::thread([this, segments = std::move(segments)]() mutable {
        // 三段音频只属于 writer 线程；start 之后 player 不会再访问它们。
        publish_paced(segments.start);
        // 按住期间重复 loop；松手或 stop 后写 end，保证有落地段。
        while (hold_.load() && running_.load()) {
            publish_paced(segments.loop);
        }
        publish_paced(segments.end);
        publisher_.publish_eos();
        finished_ = true;
    });
}

void WheeePlayer::tick() {
    {
        std::scoped_lock lock(mutex_);
        if (!running_.load()) {
            return;
        }
        // 300 ms 没有新 hold=true 视为客户端掉线：落地而不是永远循环。
        if (hold_.load() && std::chrono::steady_clock::now() - last_hold_ > kHoldTimeout) {
            hold_ = false;
        }
        if (!finished_.load()) {
            return;
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    {
        std::scoped_lock lock(mutex_);
        running_ = false;
        hold_ = false;
        session_.release(AudioOwner::Wheee);
    }
}

void WheeePlayer::stop() {
    {
        std::scoped_lock lock(mutex_);
        hold_ = false;
        running_ = false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::scoped_lock lock(mutex_);
    session_.release(AudioOwner::Wheee);
    finished_ = false;
}

// 按 10 ms 块发送，并用 steady_clock 控制节奏，避免 DDS/pipe 累积。
void WheeePlayer::publish_paced(const WavPcm& segment) {
    const std::size_t total = segment.frames();
    auto deadline = std::chrono::steady_clock::now();
    for (std::size_t offset = 0; offset < total && running_.load(); offset += kChunkFrames) {
        const std::size_t frames = std::min(kChunkFrames, total - offset);
        std::array<int16_t, kChunkFrames> samples{};
        std::memcpy(samples.data(), segment.data.data() + offset * sizeof(int16_t),
                    frames * sizeof(int16_t));
        publisher_.publish(samples.data(), frames, false);
        deadline += kChunkPeriod;
        std::this_thread::sleep_until(deadline);
    }
}

bool WheeePlayer::load_segments(Segments* segments, std::string* error) const {
    const std::filesystem::path directory = std::filesystem::path(runtime_.bank()) / "wheee";
    std::vector<std::filesystem::path> starts;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.rfind("wheee_start_", 0) == 0 &&
            entry.path().extension() == ".wav") {
            starts.push_back(entry.path());
        }
    }
    if (starts.empty()) {
        if (error != nullptr) {
            *error = "no wheee_start_*.wav in " + directory.string();
        }
        return false;
    }
    std::sort(starts.begin(), starts.end());
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto& start_path = starts[static_cast<std::size_t>(now) % starts.size()];
    const std::string filename = start_path.filename().string();
    // 文件名 wheee_start_<letter>.wav → 同一 letter 的 loop/end。
    const std::string letter = filename.substr(std::string{"wheee_start_"}.size(),
                                               filename.size() - std::string{"wheee_start_"}.size() -
                                                       std::string{".wav"}.size());
    const auto loop_path = directory / ("wheee_loop_" + letter + ".wav");
    const auto end_path = directory / ("wheee_end_" + letter + ".wav");

    std::string detail;
    const auto start = WavPcmReader::read(start_path, &detail);
    if (!start) {
        if (error != nullptr) {
            *error = detail;
        }
        return false;
    }
    const auto loop = WavPcmReader::read(loop_path, &detail);
    if (!loop) {
        if (error != nullptr) {
            *error = detail;
        }
        return false;
    }
    const auto end = WavPcmReader::read(end_path, &detail);
    if (!end) {
        if (error != nullptr) {
            *error = detail;
        }
        return false;
    }
    segments->start = *start;
    segments->loop = *loop;
    segments->end = *end;
    return true;
}

} // namespace duck_audio
