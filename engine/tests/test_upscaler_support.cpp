// SPDX-License-Identifier: GPL-2.0-or-later
// Saved upscaler choices must be safe before the first rendered frame: one the GPU or driver cannot run falls back
// as ConfigureUpscalerSupport has it (DLSS to FSR 3.1; FSR 4.1.1 to FSR 4 where FSR 4 runs, else FSR 3.1; FSR 4 to
// FSR 3.1), the preset and the output kept. Runs on Linux and Windows (its settings file in the temporary folder).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <random>
#include <string>
#include "bbport_settings.h"

namespace {
void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}
// The output list's index of a size (the list grows; its indices move).
int Output(int width, int height) {
    for (int i = 0; i < BbSettings::OutputCount; ++i)
        if (BbSettings::OutputWidths[i] == width && BbSettings::OutputHeights[i] == height) return i;
    return -1;
}
// What a requested upscaler becomes on a GPU and driver with these.
int Supported(int requested, bool fsr4, bool fsr411, bool dlss) {
    using namespace BbSettings;
    if (requested == UpscalerDlss) return dlss ? requested : UpscalerFsr3;
    if (requested == UpscalerFsr411) return fsr4 && fsr411 ? requested : fsr4 ? UpscalerFsr4 : UpscalerFsr3;
    if (requested == UpscalerFsr4) return fsr4 ? requested : UpscalerFsr3;
    return requested;
}
} // namespace

int main() {
    using namespace BbSettings;
    const std::string path = (std::filesystem::temp_directory_path() /
                              ("bbport-upscaler-test-" + std::to_string(std::random_device{}()) + ".ini"))
                                 .string();
    { FILE* config = std::fopen(path.c_str(), "w"); assert(config); std::fclose(config); }
    SetEnv("BB_CONFIG", path.c_str());
    SetEnv("BB_UPSCALER", nullptr);
    SetEnv("BB_UPSCALE_PRESET", nullptr);
    SetEnv("BB_RENDER_RES", nullptr);
    const int qhd = Output(2560, 1440);
    assert(qhd >= 0);
    auto& s = Get();
    s.upscaler = UpscalerTaa;
    s.preset = Performance;
    assert(RenderPreset() == NativeAA && s.preset == Performance);
    Save();
    s.upscaler = UpscalerOff;
    Load();
    assert(s.upscaler == UpscalerTaa && s.preset == Performance);
    assert(RenderPreset() == NativeAA && !ResolutionNeedsRestart());
    s.upscaler = UpscalerFsr3;
    assert(RenderPreset() == Performance);
    for (bool fsr4 : {false, true}) {
        for (bool fsr411 : {false, true}) {
            for (bool dlss : {false, true}) {
                for (int requested = 0; requested < UpscalerCount; ++requested) {
                    FILE* config = std::fopen(path.c_str(), "w");
                    assert(config);
                    std::fprintf(config, "upscaler=%s\npreset=3\noutput_res=2560x1440\n", UpscalerName(requested));
                    std::fclose(config);
                    s.fsr4_problem = nullptr;
                    Load();
                    assert(s.upscaler == requested);
                    const int expected = Supported(requested, fsr4, fsr411, dlss);
                    ConfigureUpscalerSupport(fsr4, fsr411, dlss);
                    assert(s.upscaler == expected);
                    assert(s.fsr4_supported == fsr4);
                    assert(s.fsr411_supported == (fsr4 && fsr411));
                    assert(s.dlss_supported == dlss);
                    assert(bool(s.fsr4_problem.load()) == (expected != requested));
                    assert(s.preset == Performance && s.output_res == qhd);
                    // Startup patch settings still describe the already applied guest patches.
                    assert(s.startup_upscaler == requested);
                    // Once more changes nothing: the fallback is already supported.
                    ConfigureUpscalerSupport(fsr4, fsr411, dlss);
                    assert(s.upscaler == expected);
                }
            }
        }
    }
    s.startup_preset = Quality;
    s.startup_upscaler = UpscalerFsr3;
    s.upscaler = UpscalerFsr3;
    s.preset = Performance;
    assert(RenderPreset() == Performance && !ResolutionNeedsRestart());
    SetEnv("BB_RENDER_RES", "1706x960");
    assert(RenderPreset() == Quality && ResolutionNeedsRestart());
    s.preset = Quality;
    assert(!ResolutionNeedsRestart());
    s.upscaler = UpscalerOff;
    assert(ResolutionNeedsRestart());
    s.upscaler = UpscalerFsr4;
    assert(!ResolutionNeedsRestart()); // provider change at the same patched size is live
    s.upscaler = UpscalerTaa;          // TAA needs native guest targets
    assert(ResolutionNeedsRestart());
    s.upscaler = UpscalerFsr4;
    const int output = s.startup_output_res;
    s.output_res = (output + 1) % OutputCount;
    assert(ResolutionNeedsRestart());
    s.output_res = output;
    SetEnv("BB_RENDER_RES", nullptr);
    assert(!FixedRenderSession() && !ResolutionNeedsRestart());
    std::remove(path.c_str());
    std::puts("Upscaler support: saved choices and their fallbacks PASS");
}
