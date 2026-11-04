#include "duck_audio/wav_pcm_reader.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace duck_audio {
    namespace {

        uint16_t read_u16(const std::uint8_t* data) {
            return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
        }

        uint32_t read_u32(const std::uint8_t* data) {
            return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
        }

    } // namespace

    // 最小 RIFF 走查：只读 fmt/data，忽略 LIST 等 chunk；所有长度都做边界检查。
    std::optional<WavPcm> WavPcmReader::read(const std::filesystem::path& path, std::string* error) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            if (error != nullptr) {
                *error = "cannot open " + path.string();
            }
            return std::nullopt;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                              std::istreambuf_iterator<char>());
        if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
            std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
            if (error != nullptr) {
                *error = "not a RIFF/WAVE file: " + path.string();
            }
            return std::nullopt;
        }

        WavPcm wav;
        bool have_format = false;
        bool have_data = false;
        std::size_t at = 12;
        while (at + 8 <= bytes.size()) {
            const std::uint8_t* chunk = bytes.data() + at;
            const std::uint32_t length = read_u32(chunk + 4);
            const std::size_t body = at + 8;
            if (body + length > bytes.size()) {
                if (error != nullptr) {
                    *error = "truncated WAV chunk: " + path.string();
                }
                return std::nullopt;
            }
            if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
                const std::uint16_t format = read_u16(bytes.data() + body);
                wav.channels = read_u16(bytes.data() + body + 2);
                wav.sample_rate = read_u32(bytes.data() + body + 4);
                wav.bits_per_sample = read_u16(bytes.data() + body + 14);
                if (format != 1) {
                    if (error != nullptr) {
                        *error = "unsupported WAV format (only PCM): " + path.string();
                    }
                    return std::nullopt;
                }
                have_format = true;
            }
            else if (std::memcmp(chunk, "data", 4) == 0) {
                // 保留原始 S16LE 字节；AudioStream 直接按字节发送，不做二次量化。
                wav.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                                bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
                have_data = true;
            }
            at = body + length + (length & 1U);
        }

        // 音库/流格式固定；其他格式宁可拒绝，也不在播放线程做隐式重采样。
        if (!have_format || !have_data || wav.channels != 1 || wav.bits_per_sample != 16 ||
            wav.sample_rate != 48000) {
            if (error != nullptr) {
                *error = "bank WAV must be 48 kHz mono 16-bit: " + path.string();
            }
            return std::nullopt;
        }
        if (wav.data.size() % sizeof(int16_t) != 0) {
            wav.data.pop_back();
        }
        return wav;
    }

} // namespace duck_audio
