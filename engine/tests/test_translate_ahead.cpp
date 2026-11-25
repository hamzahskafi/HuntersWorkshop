// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 17 part 2 (shadps4/video_core/renderer_vulkan/translate_ahead.h): the registry stage A
// finds translations queued ahead in, and a job's progress: found by program, stage and inputs, taken
// out when used, old and surplus entries dropped; one thread starts a job, every waiter sees it end.
#include "video_core/renderer_vulkan/translate_ahead.h"

#include <cstdio>
#include <thread>
#include <vector>

using namespace Vulkan::Ahead;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

/// A stand-in job: the inputs of each stage it translates.
struct Job {
    std::vector<int> inputs;
    Progress progress;
};

std::shared_ptr<Job> MakeJob(std::vector<int> inputs) {
    auto job = std::make_shared<Job>();
    job->inputs = std::move(inputs);
    return job;
}

} // namespace

int main() {
    constexpr std::uint64_t s = 1'000'000'000ull; // a second in ns

    {
        Registry<Job> reg{30 * s, 100};
        const auto a = MakeJob({7, 8});  // a chain: program 0xa (stage 1) then 0xb (stage 0)
        const auto b = MakeJob({9});
        reg.Add(0xa, 1, a, 0, 0);
        reg.Add(0xb, 0, a, 1, 0);
        reg.Add(0xa, 1, b, 0, 0);
        check(reg.Size() == 3, "every stage of every job is an entry");
        const auto input = [](int want) {
            return [want](const Job& job, std::uint32_t stage) { return job.inputs[stage] == want; };
        };
        const auto first = reg.Find(0xa, 1, input(7));
        check(first && first->job == a && first->stage == 0, "found by program, stage and inputs");
        const auto other = reg.Find(0xa, 1, input(9));
        check(other && other->job == b, "another job for the same program, other inputs");
        check(!reg.Find(0xa, 1, input(5)), "no job with these inputs");
        check(!reg.Find(0xa, 0, input(7)), "the same program at another hardware stage is another key");
        const auto second = reg.Find(0xb, 0, input(8));
        check(second && second->job == a && second->stage == 1, "a chain's later stage found by its own program");
        reg.Remove(0xa, 1, a.get(), 0);
        check(!reg.Find(0xa, 1, input(7)), "a used stage is gone");
        check(reg.Find(0xb, 0, input(8)).has_value(), "the job's other stage stays");
        check(reg.Find(0xa, 1, input(9)).has_value(), "the other job for that program stays");
        check(reg.Size() == 2, "the count follows removals");
        reg.Remove(0xa, 1, a.get(), 0);
        check(reg.Size() == 2, "removing twice changes nothing");
    }
    {
        Registry<Job> reg{10 * s, 100};
        reg.Add(1, 0, MakeJob({1}), 0, 0);
        reg.Add(2, 0, MakeJob({2}), 0, 5 * s);
        reg.Add(3, 0, MakeJob({3}), 0, 12 * s); // the first is 12 s old now: dropped
        const auto any = [](const Job&, std::uint32_t) { return true; };
        check(!reg.Find(1, 0, any), "an entry no draw took goes after its age");
        check(reg.Find(2, 0, any) && reg.Find(3, 0, any), "younger entries stay");
        check(reg.Size() == 2, "the count follows expiry");
    }
    {
        Registry<Job> reg{1000 * s, 3};
        for (int i = 0; i < 5; ++i) {
            reg.Add(std::uint64_t(i), 0, MakeJob({i}), 0, std::uint64_t(i) * s);
        }
        const auto any = [](const Job&, std::uint32_t) { return true; };
        check(reg.Size() == 3, "never more entries than the cap");
        check(!reg.Find(0, 0, any) && !reg.Find(1, 0, any), "the oldest go first");
        check(reg.Find(2, 0, any) && reg.Find(4, 0, any), "the newest stay");
    }
    {
        // One starter among many threads; every waiter sees the end.
        for (int round = 0; round < 50; ++round) {
            Progress progress;
            std::atomic<int> started{0};
            std::vector<std::thread> threads;
            for (int t = 0; t < 8; ++t) {
                threads.emplace_back([&] {
                    if (progress.Start()) {
                        started.fetch_add(1);
                        progress.Finish(true);
                    } else {
                        check(progress.Wait() == State::Done, "a waiter sees the job done");
                    }
                });
            }
            for (auto& t : threads) {
                t.join();
            }
            check(started.load() == 1, "exactly one thread starts a job");
        }
        Progress failed;
        check(failed.Start(), "a queued job can be started");
        check(!failed.Start(), "a started job cannot be started again");
        failed.Finish(false);
        check(failed.Wait() == State::Failed && failed.Get() == State::Failed, "a failed job is seen as failed");
    }

    if (g_fail) {
        std::printf("translate-ahead-test: %d failed\n", g_fail);
        return 1;
    }
    std::printf("translate-ahead-test: ok\n");
    return 0;
}
