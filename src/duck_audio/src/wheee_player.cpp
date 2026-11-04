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
    thread_ = std::thread([this, segments = std::move(segments)]() mutable {
        // Segments are local to the writer; the player never touches them after start.
        publish_paced(segments.start);
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
