// SPDX-License-Identifier: GPL-2.0-or-later
// The start's splash's status file (gpu/splash/status.h): its records read back, records half
// written or unknown passed over, and the file written in place as bb-start, run.sh and the engine
// write it.

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "status_file.h"

using namespace BbSplashStatus;

int main() {
    // Every step's record reads back, 64 bytes each.
    for (int s = 0; s < kStepCount; ++s) {
        const std::string record = Record(7, Step(s), 812, 1173);
        assert(record.size() == kRecordSize);
        assert(record.back() == '\n');
        const auto status = Parse(record);
        assert(status && int(status->step) == s && status->done == 812 && status->total == 1173);
    }
    // run.sh's records (printf '%-63s\n').
    {
        char text[80];
        std::snprintf(text, sizeof text, "%-63s\n", "2 engine 0 0 2");
        const auto status = Parse(text);
        assert(status && status->step == Step::Engine && status->total == 0);
    }
    // Half written: a newer record over the start of an older one, or cut short.
    struct Pair {
        Step old_step, new_step;
        std::uint64_t old_done, new_done;
    };
    for (const Pair pair : {Pair{Step::Shaders, Step::Shaders, 1100, 1101}, Pair{Step::Shaders, Step::Shaders, 999, 1000},
                            Pair{Step::Catalog, Step::Catalog, 100, 99}, Pair{Step::Catalog, Step::Shaders, 1109, 0},
                            Pair{Step::Starting, Step::Close, 0, 0}}) {
        const std::string old_record = Record(4, pair.old_step, pair.old_done, 1173);
        const std::string new_record = Record(5, pair.new_step, pair.new_done, 1173);
        for (size_t cut = 0; cut <= kRecordSize; ++cut) {
            const auto status = Parse(new_record.substr(0, cut) + old_record.substr(cut));
            // Either record whole, or nothing: never a third reading.
            assert(!status || (status->step == pair.old_step && status->done == pair.old_done) ||
                   (status->step == pair.new_step && status->done == pair.new_done));
        }
    }
    const std::string new_record = Record(5, Step::Shaders, 1101, 1173);
    assert(!Parse(""));
    assert(!Parse(new_record.substr(0, 20)));
    assert(!Parse("4 shaders 50x 1173 4" + std::string(43, ' ') + "\n"));
    assert(!Parse("4 lunch 0 0 4" + std::string(50, ' ') + "\n"));
    assert(!Parse("4 shaders 1 2 5" + std::string(48, ' ') + "\n"));
    // The writer: made by the start, then written over in place, never made again once removed.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "bb-splash-status-test.txt";
    std::filesystem::remove(path);
    {
        Writer start;
        assert(!start.Open(path, false)); // the engine's open never makes it
        assert(start.Open(path, true));
        start.Write(Step::Prepare);
        start.Close();
        Writer engine;
        assert(engine.Open(path, false));
        engine.Write(Step::Catalog, 486, 1109);
        engine.Close();
    }
    {
        std::ifstream in(path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        assert(text.size() == kRecordSize);
    }
    {
        // The splash's reader, open while the file is written over; it removes the file as it closes.
        Reader splash(path);
        auto status = splash.Read();
        assert(status && status->step == Step::Catalog && status->done == 486 && status->total == 1109);
        Writer engine;
        assert(engine.Open(path, false));
        engine.Write(Step::Close);
        engine.Close();
        status = splash.Read();
        assert(status && status->step == Step::Close);
        splash.Remove();
        assert(!std::filesystem::exists(path));
    }
    {
        Writer engine;
        assert(!engine.Open(path, false));
        assert(!std::filesystem::exists(path));
    }
    std::puts("splash status: ok");
    return 0;
}
