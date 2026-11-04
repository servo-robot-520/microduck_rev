#include "duck_audio/voice_module.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>

#include "audio_io_interface/ros_msg.hpp"

namespace duck_audio {
namespace {

bool is_one_shot(const std::string& tag) {
    return tag == "alarm" || tag == "greet" || tag == "inquire" || tag == "peck" ||
           tag == "chirp" || tag == "coo";
}

} // namespace

VoiceModule::VoiceModule(rclcpp::Node& node, VoiceRuntime& runtime, AudioSession& session)
    : node_(node), runtime_(runtime), session_(session) {
    publisher_ = node_.create_publisher<audio_io_interface::msg::AudioClip>(
            audio_io_interface::topic_play(), rclcpp::QoS(rclcpp::KeepLast(10)).reliable());
}

VoiceModule::PlayResult VoiceModule::play(const std::string& tag, const uint32_t variant) {
    if (!runtime_.has_voice()) {
        return reject("this robot has no voice bank");
    }
    if (!is_one_shot(tag)) {
        return reject("unknown or non-one-shot tag: " + tag);
    }
    if (session_.owner() != AudioOwner::None) {
        return reject("a live voice stream owns the audio channel");
    }

    std::string error;
    const std::string path = choose_wav(tag, variant, &error);
    if (path.empty()) {
        return reject(std::move(error));
    }

    const std::string request_id = tag + "-" + std::to_string(++counter_);
    audio_io_interface::msg::AudioClip clip;
    clip.header.stamp = node_.now();
    clip.header.frame_id = "duck_audio";
    clip.request_id = request_id;
    clip.uri = "file://" + std::filesystem::absolute(path).string();
    clip.format = "wav";
    clip.gain = 1.0F;
    clip.loop = false;
    clip.group = "voice";
    if (tag == "peck" || tag == "alarm") {
        clip.priority = audio_io_interface::msg::AudioClip::PRIORITY_ALERT;
        clip.policy = audio_io_interface::msg::AudioClip::POLICY_PREEMPT;
    } else if (tag == "coo") {
        clip.priority = audio_io_interface::msg::AudioClip::PRIORITY_AMBIENT;
        clip.policy = audio_io_interface::msg::AudioClip::POLICY_QUEUE;
    } else {
        clip.priority = audio_io_interface::msg::AudioClip::PRIORITY_INTERACTION;
        clip.policy = tag == "greet"
                              ? audio_io_interface::msg::AudioClip::POLICY_REPLACE_SAME_GROUP
                              : audio_io_interface::msg::AudioClip::POLICY_PREEMPT;
    }
    publisher_->publish(std::move(clip));

    {
        std::scoped_lock lock(tag_mutex_);
        last_tag_ = tag;
    }
    return PlayResult{true, request_id, {}};
}

std::string VoiceModule::last_tag() const {
    std::scoped_lock lock(tag_mutex_);
    return last_tag_;
}

VoiceModule::PlayResult VoiceModule::reject(std::string reason) const {
    return PlayResult{false, {}, std::move(reason)};
}

std::string VoiceModule::choose_wav(const std::string& tag, const uint32_t variant,
                                    std::string* error) const {
    const std::filesystem::path directory = std::filesystem::path(runtime_.bank()) / tag;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
            files.push_back(entry.path());
        }
    }
    if (files.empty()) {
        if (error != nullptr) {
            *error = "no WAV files for tag " + tag;
        }
        return {};
    }
    std::sort(files.begin(), files.end());
    std::size_t index = 0;
    if (variant == 0xFFFFFFFFU) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        index = static_cast<std::size_t>(now) % files.size();
    } else {
        index = static_cast<std::size_t>(variant) % files.size();
    }
    return files[index].string();
}

} // namespace duck_audio
