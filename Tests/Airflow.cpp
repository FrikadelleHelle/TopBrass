#include "Airflow.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace topbrass;
namespace {
void check(bool good, const char* message) {
    if (!good)
        throw std::runtime_error(message);
}
void word(std::ostream& out, std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
        out.put(static_cast<char>(n >> (i * 8)));
}
void scalar(std::ostream& out, float x) { out.write(reinterpret_cast<const char*>(&x), 4); }
void fixture(const std::filesystem::path& path, bool invalid = false, bool varied = false) {
    std::ofstream out(path, std::ios::binary);
    out.write("TBAIR001", 8);
    for (int value : {44100, 10000, 2000, 10000, 36, 68, 50})
        word(out, value);
    scalar(out, .1f);
    scalar(out, .4f);
    for (int i = 0; i < 10000; ++i)
        scalar(out, invalid ? 2.0f : varied ? .02f + i * .000001f + .005f * std::sin(i * .017f) : .03f);
}
struct Audio {
    std::vector<float> l, r;
    explicit Audio(int n) : l(n), r(n) {};
};
Audio play(const AirflowRecording& recording, int block, int gateEnd = 20000, float expression = 1) {
    AirflowPlayer player(recording);
    player.prepare(48000);
    AirflowControls controls;
    controls.amount = 1;
    controls.expression = expression;
    Audio out(30000);
    std::array<AirflowFrame, 128> input{};
    for (int offset = 0; offset < 30000;) {
        int count = std::min({block, 128, 30000 - offset});
        for (int i = 0; i < count; ++i)
            input[i] = {offset + i < gateEnd ? 1.0f : 0, 50, 1};
        player.render(1, std::span(input.data(), count), controls, out.l.data() + offset, out.r.data() + offset);
        offset += count;
    }
    check(player.activeVoices() == 0, "Airflow must finish its bounded release");
    return out;
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected fixture directory");
        const auto path = std::filesystem::path(argv[1]) / "airflow-fixture.bank";
        fixture(path);
        AirflowRecording recording;
        std::string error;
        check(recording.load(path, error), error.c_str());
        const auto a = play(recording, 128), b = play(recording, 31);
        check(a.l == b.l && a.r == b.r, "Airflow must be invariant to block subdivision and native-loop wrapping");
        check(std::abs(a.l[1000] - .03f * .4f * .5f * .70710678f) < 1e-7f,
              "Settled airflow must preserve source PCM and calibration");
        const auto zero = play(recording, 128, 20000, 0);
        check(std::all_of(zero.l.begin(), zero.l.end(), [](float x) { return x == 0; }),
              "Pressure zero must suppress airflow from its first frame");
        AirflowPlayer player(recording);
        player.prepare(48000);
        AirflowControls controls;
        controls.amount = 1;
        std::array<AirflowFrame, 128> input{};
        for (auto& f : input)
            f = {1, 50, 1};
        Audio out(128);
        player.render(1, input, controls, out.l.data(), out.r.data());
        check(player.activeVoices() == 1, "One active channel means one airflow stream");
        player.stop(1);
        std::fill(out.l.begin(), out.l.end(), 0);
        std::fill(out.r.begin(), out.r.end(), 0);
        for (auto& f : input)
            f = {0, 50, 0};
        player.render(1, input, controls, out.l.data(), out.r.data());
        check(player.activeVoices() == 0 && std::all_of(out.l.begin(), out.l.end(), [](float x) { return x == 0; }),
              "Immediate stop must not leave airflow tails");
        fixture(path, true);
        check(!recording.load(path, error), "Invalid airflow PCM must fail without replacing the recording");
        check(recording.audio[0] == .03f, "Failed airflow load must preserve valid PCM");
        fixture(path, false, true);
        check(recording.load(path, error), error.c_str());
        AirflowPlayer varied(recording);
        varied.prepare(48000);
        Audio actual(30000);
        for (int offset = 0; offset < 30000; offset += 128) {
            int count = std::min(128, 30000 - offset);
            for (int i = 0; i < count; ++i)
                input[i] = {1, offset + i < 12000 ? 50 : 62, 1};
            varied.render(1, std::span(input.data(), count), controls, actual.l.data() + offset,
                          actual.r.data() + offset);
        }
        double position = 0, increment = 44100.0 / 48000;
        bool looped = false;
        float worst = 0;
        const float smoothing = 1 - std::exp(-1.0f / (48000 * .008f));
        for (int frame = 0; frame < 30000; ++frame) {
            const double desired = 44100.0 / 48000 * std::exp2((frame < 12000 ? 0 : 12) * .1 / 12);
            increment += smoothing * (desired - increment);
            const int index = static_cast<int>(position);
            const float t = static_cast<float>(position - index), t2 = t * t, t3 = t2 * t;
            const auto tap = [&](int n) {
                if (n >= 10000)
                    n = 2000 + n - 10000;
                if (looped && n < 2000)
                    n = 10000 - (2000 - n);
                return recording.audio[static_cast<std::size_t>(std::clamp(n, 0, 9999))];
            };
            const float expected =
                (tap(index - 1) * (-.5f * t + t2 - .5f * t3) + tap(index) * (1 - 2.5f * t2 + 1.5f * t3) +
                 tap(index + 1) * (.5f * t + 2 * t2 - 1.5f * t3) + tap(index + 2) * (-.5f * t2 + .5f * t3)) *
                .4f * .5f * .70710678f;
            if (frame > 1000)
                worst = std::max(worst, std::abs(actual.l[static_cast<std::size_t>(frame)] - expected));
            position += increment;
            if (position >= 10000) {
                position = 2000 + std::fmod(position - 10000, 8000);
                looped = true;
            }
        }
        std::cout << "Nonconstant source/rate/loop/pitch oracle error=" << worst << '\n';
        check(worst < 1e-6f,
              "Fractional native-loop interpolation and ten-percent pitch tracking must match an independent oracle");
        std::filesystem::remove(path);
        std::cout << "PASS: airflow PCM/calibration, pressure, bounded release, native loop, source rate and block "
                     "invariance\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
