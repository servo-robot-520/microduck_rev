// microduck_sounds C ABI 的 C++ 冒烟测试：覆盖 ROS 2 节点会用的边界——
// 生命周期、10 ms 流块、score/cast 和 beat 时钟。
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "microduck_sounds.h"

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (last error: %s)\n", what, duck_last_error());
        ++failures;
    }
}
}  // namespace

int main() {
    check(duck_abi_version() == MICRODUCK_SOUNDS_ABI_VERSION, "ABI version");
    check(duck_bank_version() == 5, "bank version");

    DuckVoice* voice = duck_voice_new(100);
    check(voice != nullptr, "voice created");
    DuckPersonality personality{};
    check(duck_voice_personality(voice, &personality) == DUCK_OK, "personality copied");
    check(personality.seed == 100, "personality seed");
    check(personality.pitch_center_hz >= 110.0 && personality.pitch_center_hz <= 620.0,
          "pitch centre range");

    DuckStream* stream = duck_stream_new_wheee(voice, 0);
    check(stream != nullptr, "wheee stream created");
    check(duck_stream_set(stream, 300.0, 1.0, 0.5) == DUCK_OK, "stream set");
    std::vector<float> block(480, 0.0F);
    check(duck_stream_block(stream, block.data(), block.size()) == DUCK_OK, "stream block");
    bool finite = true;
    float peak = 0.0F;
    for (float sample : block) {
        finite = finite && std::isfinite(sample);
        peak = std::max(peak, std::abs(sample));
    }
    check(finite, "stream block finite");
    check(peak > 0.0F && peak <= 1.0F, "stream block bounded");
    check(duck_stream_is_silent(stream) == 0, "a keyed stream is not silent");
    check(duck_stream_set(stream, std::numeric_limits<double>::quiet_NaN(), 1.0, 0.0) ==
              DUCK_ERR_INVALID_ARGUMENT,
          "NaN is rejected");

    float* rendered = nullptr;
    std::size_t rendered_frames = 0;
    char error[256]{};
    check(duck_voice_render(voice, "chirp", 0, &rendered, &rendered_frames, error,
                            sizeof(error)) == DUCK_OK,
          "render to memory");
    check(rendered != nullptr && rendered_frames > 1000, "rendered buffer");
    duck_buffer_free(rendered, rendered_frames);

    double low = 0.0;
    double high = 0.0;
    duck_range_hz(voice, &low, &high);
    check(duck_hz_at(voice, 0.0) == low, "hz_at low");
    check(duck_hz_at(voice, 1.0) == high, "hz_at high");

    DuckScore* score = duck_score_wistful(nullptr, 0);
    check(score != nullptr, "wistful score");
    check(duck_score_note_count(score) > 0, "score has notes");
    check(duck_score_beat_s(score) > 0.0, "score beat");
    DuckNote note{};
    check(duck_score_note(score, 0, &note) == DUCK_OK, "score note");
    check(note.beats > 0.0, "note length");

    const std::uint32_t seeds[4] = {100, 101, 102, 103};
    DuckSinger singers[4]{};
    check(duck_cast(seeds, 4, singers, 4) == DUCK_OK, "cast quartet");
    std::uint32_t part_mask = 0;
    for (const auto& singer : singers) {
        part_mask |= 1u << static_cast<unsigned>(singer.part);
    }
    check(part_mask == 0x0Fu, "cast fills all four parts");
    check(duck_cast(seeds, 4, singers, 3) == DUCK_ERR_BUFFER_TOO_SMALL,
          "cast refuses a short buffer");

    const double registers[4] = {200.0, 500.0, 300.0, 400.0};
    std::int32_t parts[4]{};
    check(duck_seat_all(registers, 4, parts, 4) == DUCK_OK, "seat_all");
    check(parts[0] == DUCK_PART_BASS && parts[1] == DUCK_PART_SOPRANO &&
              parts[2] == DUCK_PART_TENOR && parts[3] == DUCK_PART_ALTO,
          "seat_all maps registers to parts");

    DuckConductor* conductor = duck_conductor_new(60.0, 0.0);
    check(conductor != nullptr, "conductor");
    std::uint64_t beat = 0;
    check(duck_conductor_due(conductor, 0.0, &beat) == 1 && beat == 0, "first beat");
    check(duck_conductor_due(conductor, 0.5, &beat) == 0, "same beat");
    check(duck_conductor_due(conductor, 1.0, &beat) == 1 && beat == 1, "next beat");

    DuckFollower* follower = duck_follower_new(60.0);
    check(follower != nullptr, "follower");
    for (int i = 0; i < 4; ++i) {
        duck_follower_observe(follower, static_cast<std::uint8_t>(i), static_cast<double>(i));
    }
    check(duck_follower_locked(follower) == 1, "follower locked");
    double position = 0.0;
    check(duck_follower_position_beats(follower, 4.0, &position) == 1, "follower position");
    check(std::abs(position - 4.0) < 0.01, "follower position value");

    duck_follower_free(follower);
    duck_conductor_free(conductor);
    duck_score_free(score);
    duck_stream_free(stream);
    duck_voice_free(voice);

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("microduck_sounds FFI smoke test: OK");
    return 0;
}
