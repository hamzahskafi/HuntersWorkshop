// SPDX-License-Identifier: GPL-2.0-or-later
// Saved upscaler choices must be safe before the first rendered frame: one the GPU or driver cannot run falls back
// as ConfigureUpscalerSupport has it (0.4.4 beta 27: DLSS to AMD FSR where it runs, else TAA; AMD FSR to TAA), the
// preset and the output kept. Runs on Linux and Windows (its settings file in the temporary folder).
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
int Supported(int requested, bool fsr, bool dlss) {
    using namespace BbSettings;
    if (requested == UpscalerDlss) return dlss ? requested : fsr ? UpscalerFsr : UpscalerTaa;
    if (requested == UpscalerFsr) return fsr ? requested : UpscalerTaa;
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
    s.upscaler = UpscalerFsr;
    assert(RenderPreset() == Performance);
    for (bool fsr : {false, true}) {
        for (bool dlss : {false, true}) {
            for (int requested = 0; requested < UpscalerCount; ++requested) {
                FILE* config = std::fopen(path.c_str(), "w");
                assert(config);
                std::fprintf(config, "upscaler=%s\npreset=3\noutput_res=2560x1440\n", UpscalerName(requested));
                std::fclose(config);
                s.fsr4_problem = nullptr;
                Load();
                assert(s.upscaler == requested);
                const int expected = Supported(requested, fsr, dlss);
                ConfigureUpscalerSupport(fsr, dlss);
                assert(s.upscaler == expected);
                assert(s.fsr_supported == fsr);
                assert(s.dlss_supported == dlss);
                assert(bool(s.fsr4_problem.load()) == (expected != requested));
                assert(s.preset == Performance && s.output_res == qhd);
                // Startup patch settings still describe the already applied guest patches.
                assert(s.startup_upscaler == requested);
                // Once more changes nothing: the fallback is already supported.
                ConfigureUpscalerSupport(fsr, dlss);
                assert(s.upscaler == expected);
            }
        }
    }
    // 0.4.4 beta 27: the older FSR names read as AMD's FSR.
    for (const char* old : {"fsr3", "fsr4", "fsr411"}) {
        FILE* config = std::fopen(path.c_str(), "w");
        assert(config);
        std::fprintf(config, "upscaler=%s\n", old);
        std::fclose(config);
        s.upscaler = UpscalerOff;
        Load();
        assert(s.upscaler == UpscalerFsr);
    }
    s.startup_preset = Quality;
    s.startup_upscaler = UpscalerFsr;
    s.upscaler = UpscalerFsr;
    s.preset = Performance;
    assert(RenderPreset() == Performance && !ResolutionNeedsRestart());
    SetEnv("BB_RENDER_RES", "1706x960");
    assert(RenderPreset() == Quality && ResolutionNeedsRestart());
    s.preset = Quality;
    assert(!ResolutionNeedsRestart());
    s.upscaler = UpscalerOff;
    assert(ResolutionNeedsRestart());
    s.upscaler = UpscalerDlss;
    assert(!ResolutionNeedsRestart()); // provider change at the same patched size is live
    s.upscaler = UpscalerTaa;          // TAA needs native guest targets
    assert(ResolutionNeedsRestart());
    s.upscaler = UpscalerFsr;
    const int output = s.startup_output_res;
    s.output_res = (output + 1) % OutputCount;
    assert(ResolutionNeedsRestart());
    s.output_res = output;
    SetEnv("BB_RENDER_RES", nullptr);
    assert(!FixedRenderSession() && !ResolutionNeedsRestart());
    std::remove(path.c_str());
    std::puts("Upscaler support: saved choices and their fallbacks PASS");
}
