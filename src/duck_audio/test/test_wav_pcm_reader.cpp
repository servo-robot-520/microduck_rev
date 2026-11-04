#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "duck_audio/wav_pcm_reader.hpp"

namespace {

void put_u16(std::vector<std::uint8_t>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

std::filesystem::path write_wav(const std::uint32_t rate, const std::vector<std::int16_t>& samples) {
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    put_u32(bytes, 36 + data_bytes);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E'});
    bytes.insert(bytes.end(), {'f', 'm', 't', ' '});
    put_u32(bytes, 16);
    put_u16(bytes, 1);
    put_u16(bytes, 1);
    put_u32(bytes, rate);
    put_u32(bytes, rate * 2);
    put_u16(bytes, 2);
    put_u16(bytes, 16);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    put_u32(bytes, data_bytes);
    for (const auto sample : samples) {
        put_u16(bytes, static_cast<std::uint16_t>(sample));
    }
    const auto path = std::filesystem::temp_directory_path() /
                      ("duck_audio_wav_test_" + std::to_string(::getpid()) + ".wav");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST(WavPcmReader, ReadsBankFormat) {
    const auto path = write_wav(48000, {0, 1000, -1000, 2000});
    std::string error;
    const auto wav = duck_audio::WavPcmReader::read(path, &error);
    std::filesystem::remove(path);
    ASSERT_TRUE(wav.has_value()) << error;
    EXPECT_EQ(wav->sample_rate, 48000U);
    EXPECT_EQ(wav->channels, 1);
    EXPECT_EQ(wav->bits_per_sample, 16);
    EXPECT_EQ(wav->frames(), 4U);
    EXPECT_EQ(wav->data.size(), 8U);
}

TEST(WavPcmReader, RejectsOtherRates) {
    const auto path = write_wav(44100, {0, 1});
    std::string error;
    EXPECT_FALSE(duck_audio::WavPcmReader::read(path, &error).has_value());
    std::filesystem::remove(path);
    EXPECT_FALSE(error.empty());
}

TEST(WavPcmReader, RejectsMissingFile) {
    std::string error;
    EXPECT_FALSE(duck_audio::WavPcmReader::read("/definitely/not/a/file.wav", &error).has_value());
    EXPECT_FALSE(error.empty());
}
