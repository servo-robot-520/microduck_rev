#include "duck_audio/voice_runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace duck_audio {
    namespace {

        std::string trim(const std::string& value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

    } // namespace

    std::string VoiceRuntime::default_bank_path() {
        // XDG 优先；没有 XDG 时使用用户配置目录。服务没有 HOME 时退回 /var/lib。
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
            return std::string{xdg} + "/duck_ros2/sounds";
        }
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            return std::string{home} + "/.config/duck_ros2/sounds";
        }
        return "/var/lib/duck_ros2/sounds";
    }

    VoiceRuntime::VoiceRuntime(Config config, const rclcpp::Logger& logger) :
        config_(std::move(config)), logger_(logger) {
        if (config_.bank.empty()) {
            config_.bank = default_bank_path();
        }
        config_.bank = resolve_bank_path(std::move(config_.bank));
    }

    VoiceRuntime::~VoiceRuntime() {
        if (voice_ != nullptr) {
            duck_voice_free(voice_);
            voice_ = nullptr;
        }
    }

    std::string VoiceRuntime::resolve_bank_path(std::string path) {
        const auto replace_prefix = [&path](const std::string_view prefix, const std::string_view value) {
            if (path.rfind(prefix, 0) == 0) {
                path.replace(0, prefix.size(), value.data(), value.size());
                return true;
            }
            return false;
        };
        if (path == "~") {
            if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
                path = home;
            }
            else {
                path = default_bank_path();
            }
        }
        else if (path.rfind("~/", 0) == 0) {
            if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
                replace_prefix("~", home);
            }
            else {
                path = default_bank_path();
            }
        }
        else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            if (!replace_prefix("${HOME}", home)) {
                (void)replace_prefix("$HOME", home);
            }
        }
        return std::filesystem::absolute(path).lexically_normal().string();
    }

    bool VoiceRuntime::initialize() {
        // ABI 不匹配是打包错误：拒绝实时声音，而不是用错位的结构体。
        if (duck_abi_version() != MICRODUCK_SOUNDS_ABI_VERSION) {
            last_error_ = "libmicroduck_sounds ABI mismatch";
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            return false;
        }
        bank_version_ = duck_bank_version();

        // 启动时检查音库；缺失/过期/无 .seed 时自动渲染到同级 staging，再原子替换。
        if (!ensure_bank()) {
            has_voice_ = false;
            return false;
        }
        if (!scan_bank()) {
            has_voice_ = false;
            return false;
        }

        uint32_t marker_seed = 0;
        uint32_t marker_version = 0;
        if (!read_marker(&marker_seed, &marker_version)) {
            last_error_ = "voice bank has no valid .seed after generation";
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            has_voice_ = false;
            return false;
        }
        if (marker_version != bank_version_) {
            std::ostringstream message;
            message << "voice bank version " << marker_version << " does not match library "
                << bank_version_ << "; run ros2 run duck_audio sounds ensure-bank --force";
            last_error_ = message.str();
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            has_voice_ = false;
            return false;
        }

        // seed 优先级：bench 覆盖 > 音库 .seed；ensure_bank 已保证两者一致。
        seed_ = config_.seed_override != 0 ? config_.seed_override : marker_seed;
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

    bool VoiceRuntime::ensure_bank() {
        std::error_code error;
        const auto has_wavs = [&]() {
            for (std::error_code walk_error; const auto& entry : std::filesystem::recursive_directory_iterator(
                     config_.bank, std::filesystem::directory_options::skip_permission_denied,
                     walk_error)) {
                if (walk_error) {
                    break;
                }
                if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                    return true;
                }
            }
            return false;
        };

        uint32_t marker_seed = 0;
        uint32_t marker_version = 0;
        const bool has_marker = read_marker(&marker_seed, &marker_version);
        const bool current = has_wavs() && has_marker && marker_version == bank_version_ &&
            (config_.seed_override == 0 || marker_seed == config_.seed_override);
        if (current) {
            seed_ = config_.seed_override != 0 ? config_.seed_override : marker_seed;
            return true;
        }

        // 过期音库保留原 seed 重新渲染；没有 .seed 时才向硬件/本地身份要 seed。
        uint32_t seed = 0;
        if (config_.seed_override != 0) {
            seed = config_.seed_override;
        }
        else if (has_marker) {
            seed = marker_seed;
        }
        else {
            char hardware_error[256]{};
            if (duck_hardware_seed(&seed, hardware_error, sizeof(hardware_error)) != DUCK_OK) {
                last_error_ = std::string{"cannot derive a voice seed: "} + hardware_error;
                RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
                return false;
            }
        }

        const std::filesystem::path bank{config_.bank};
        const auto staging = std::filesystem::path{bank.string() + ".new"};
        std::filesystem::remove_all(staging, error);
        char render_error[512]{};
        if (duck_render_all(seed, staging.c_str(), render_error, sizeof(render_error)) != DUCK_OK) {
            last_error_ = std::string{"cannot render voice bank: "} + render_error;
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            std::filesystem::remove_all(staging, error);
            return false;
        }
        {
            std::ofstream marker(staging / ".seed", std::ios::trunc);
            marker << seed << ':' << bank_version_;
            if (!marker) {
                last_error_ = "cannot write voice bank marker";
                RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
                std::filesystem::remove_all(staging, error);
                return false;
            }
        }

        // staging 与 bank 同级，rename 是原子的；先删旧目录，避免半旧半新。
        std::filesystem::remove_all(bank, error);
        std::filesystem::create_directories(bank.parent_path(), error);
        std::filesystem::rename(staging, bank, error);
        if (error) {
            last_error_ = "cannot install voice bank at " + config_.bank + ": " + error.message();
            RCLCPP_ERROR(logger_, "%s", last_error_.c_str());
            std::filesystem::remove_all(staging, error);
            return false;
        }
        seed_ = seed;
        RCLCPP_INFO(logger_, "rendered voice bank %s seed %u bank_version %u", config_.bank.c_str(),
                    seed, bank_version_);
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
        // 只要求至少一个 WAV；具体 tag 缺失由 VoiceModule 播放时拒绝。
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
        // 标记格式固定为 "seed:bank_version"；损坏时按无标记处理，让硬件 seed 兜底。
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
        }
        catch (const std::exception&) {
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
