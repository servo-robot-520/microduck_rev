#pragma once

#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "duck_audio/ffi/microduck_sounds.h"

namespace duck_audio {

    /// 音库发现、seed 解析和 DuckVoice 句柄的生命周期。
    class VoiceRuntime {
    public:
        struct Config {
            std::string bank; ///< 默认 ~/.config/duck_ros2/sounds，可含 ~/$HOME
            uint32_t seed_override{0}; ///< 非 0 时覆盖 .seed，bench 用
        };

        VoiceRuntime(Config config, const rclcpp::Logger& logger);

        /// 默认音库目录：$XDG_CONFIG_HOME/duck_ros2/sounds，否则 $HOME/.config/duck_ros2/sounds；
        /// 两者都没有时用 /var/lib/duck_ros2/sounds 作为服务兜底。
        [[nodiscard]] static std::string default_bank_path();
        /// 释放 DuckVoice 句柄；不删除音库文件。
        ~VoiceRuntime();

        VoiceRuntime(const VoiceRuntime&) = delete;
        VoiceRuntime& operator=(const VoiceRuntime&) = delete;

        /// 校验 ABI/音库/版本/seed，并创建 DuckVoice；失败只降级声音。
        [[nodiscard]] bool initialize();

        [[nodiscard]] bool has_voice() const {
            return has_voice_;
        }

        [[nodiscard]] uint32_t seed() const {
            return seed_;
        }

        [[nodiscard]] uint32_t bank_version() const {
            return bank_version_;
        }

        [[nodiscard]] const std::string& bank() const {
            return config_.bank;
        }

        [[nodiscard]] const std::string& last_error() const {
            return last_error_;
        }

        [[nodiscard]] const DuckVoice* voice() const {
            return voice_;
        }

        /// 从 FFI 复制 21 个 Personality 字段；失败返回 nullopt。
        [[nodiscard]] std::optional<DuckPersonality> personality() const;

    private:
        /// 检查 bank 目录存在且至少有一个 WAV。
        /// 检查 bank 目录存在且至少有一个 WAV。
        [[nodiscard]] bool scan_bank();
        /// 读取 .seed 的 "seed:bank_version"；不存在/损坏返回 false。
        [[nodiscard]] bool read_marker(uint32_t* seed, uint32_t* version) const;
        /// 解析 `~`/`$HOME` 并转成绝对路径。
        [[nodiscard]] static std::string resolve_bank_path(std::string path);
        /// 缺失、无 WAV、无 .seed 或版本不匹配时，用 FFI 渲染到 staging 后原子替换。
        [[nodiscard]] bool ensure_bank();

        Config config_;
        rclcpp::Logger logger_;
        DuckVoice* voice_{nullptr};
        bool has_voice_{false};
        uint32_t seed_{0};
        uint32_t bank_version_{0};
        std::string last_error_;
    };

} // namespace duck_audio
