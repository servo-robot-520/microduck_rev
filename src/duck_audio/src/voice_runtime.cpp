#include "duck_audio/voice_runtime.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace duck_audio {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

} // namespace

VoiceRuntime::VoiceRuntime(Config config, rclcpp::Logger logger)
    : config_(std::move(config)), logger_(logger) {}

VoiceRuntime::~VoiceRuntime() {
    if (voice_ != nullptr) {
        duck_voice_free(voice_);
        voice_ = nullptr;
    }
}

bool VoiceRuntime::initialize() {
    if (duck_abi_version() != MICRODUCK_SOUNDS_ABI_VERSION) {
        last_error_ = "libmicroduck_sounds ABI mismatch";
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        return false;
    }
    bank_version_ = duck_bank_version();
    if (!scan_bank()) {
        has_voice_ = false;
        return false;
    }

    uint32_t marker_seed = 0;
    uint32_t marker_version = 0;
    const bool has_marker = read_marker(&marker_seed, &marker_version);
    if (has_marker && marker_version != bank_version_) {
        std::ostringstream message;
        message << "voice bank version " << marker_version << " does not match library " << bank_version_
                << "; run sounds ensure-bank --force";
        last_error_ = message.str();
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        has_voice_ = false;
        return false;
    }

    if (config_.seed_override != 0) {
        seed_ = config_.seed_override;
        if (has_marker && marker_seed != seed_) {
            RCLCPP_WARN(logger_, "voice.seed=%u overrides bank seed %u", seed_, marker_seed);
        }
    } else if (has_marker) {
        seed_ = marker_seed;
    } else {
        uint32_t hardware = 0;
        char error[256]{};
        if (duck_hardware_seed(&hardware, error, sizeof(error)) != DUCK_OK) {
            last_error_ = std::string{"no bank seed and no hardware seed: "} + error;
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            has_voice_ = false;
            return false;
        }
        seed_ = hardware;
        RCLCPP_WARN(logger_, "voice bank has no .seed; using hardware seed %u", seed_);
    }

    voice_ = duck_voice_new(seed_);
    if (voice_ == nullptr) {
        last_error_ = "duck_voice_new failed";
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        has_voice_ = false;
        return false;
    }
    has_voice_ = true;
    last_error_.clear();
    RCLCPP_INFO(logger_, "voice bank %s seed %u bank_version %u", config_.bank.c_str(), seed_,
                bank_version_);
    return true;
}

bool VoiceRuntime::scan_bank() {
    if (config_.bank.empty()) {
        last_error_ = "voice.bank is empty";
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(config_.bank, error)) {
        last_error_ = "voice bank directory not found: " + config_.bank;
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        return false;
    }
    std::size_t wavs = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 config_.bank, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".wav") {
            ++wavs;
            if (wavs > 0) {
                break;
            }
        }
    }
    if (wavs == 0) {
        last_error_ = "voice bank has no WAV files: " + config_.bank;
        RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
        return false;
    }
    return true;
}

bool VoiceRuntime::read_marker(uint32_t* seed, uint32_t* version) const {
    std::ifstream input(std::filesystem::path(config_.bank) / ".seed");
    if (!input) {
        return false;
    }
    std::string text;
    std::getline(input, text);
    text = trim(text);
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        RCLCPP_WARN(logger_, "ignoring malformed .seed: %s", text.c_str());
        return false;
    }
    try {
        *seed = static_cast<uint32_t>(std::stoul(text.substr(0, colon)));
        *version = static_cast<uint32_t>(std::stoul(text.substr(colon + 1)));
    } catch (const std::exception&) {
        RCLCPP_WARN(logger_, "ignoring malformed .seed: %s", text.c_str());
        return false;
    }
    return true;
}

std::optional<DuckPersonality> VoiceRuntime::personality() const {
    if (voice_ == nullptr) {
        return std::nullopt;
    }
    DuckPersonality value{};
    if (duck_voice_personality(voice_, &value) != DUCK_OK) {
        return std::nullopt;
    }
    return value;
}

} // namespace duck_audio
