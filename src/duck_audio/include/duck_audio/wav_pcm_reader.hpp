#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace duck_audio {

    /// 音库 WAV 的 PCM 数据；只接受 48 kHz / mono / 16-bit，因为 AudioStream 的输出格式固定。
    struct WavPcm {
        uint32_t sample_rate{0};
        uint16_t channels{0};
        uint16_t bits_per_sample{0};
        std::vector<uint8_t> data; ///< S16LE，已去掉 WAV header

        /// 样本帧数（不是字节数）。
        [[nodiscard]] std::size_t frames() const {
            return channels == 0 ? 0 : data.size() / (sizeof(int16_t) * channels);
        }
    };

    /// 最小 RIFF/WAVE 读取器；只支持音库实际使用的 48 kHz mono 16-bit PCM。
    class WavPcmReader {
    public:
        /// 读取音库 WAV；失败时返回 nullopt，error 写英文原因。
        [[nodiscard]] static std::optional<WavPcm> read(const std::filesystem::path& path,
                                                        std::string* error);
    };

} // namespace duck_audio
