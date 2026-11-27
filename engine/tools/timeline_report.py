#!/usr/bin/env python3
"""Reads a BB_TIMELINE file (gpu/shim/bbport_timeline.h: "ns event arg arg2" a line) and says where a
frame's time goes on its way from the game to the GPU (0.4.6 beta 18).

Per frame (from one of the game's frame-end submissions to the next):
  - the frame's time, and how long the game waited in it for its previous frame (GuestWait);
  - how long the GPU command thread had nothing to decode (stage A idle) and how long stage B waited
    for packets (its waits of 20 us or more);
  - how long the GPU had nothing to do: from its finishing a submission (GpuDone) to the next one's
    submission, when that came later. Submissions are the rasterizer's (VulkanSubmit) and the
    presenter's frame (PipeTask 5); GpuDone is seen by a thread waiting for it, a little late, so this
    is a lower bound;
  - how many submissions were made, and the share of them queued behind earlier work on the GPU.
And the way of each frame after the game submitted its end (GuestSubmitDone) to its retirement
(FrameRetired): until the GPU command thread went idle (stage A done), until the presenter submitted
the frame (PipeTask 5), and from there until the GPU had run it (the GPU's queue and work).

Usage: python3 -I tools/timeline_report.py <timeline file> [--frames N]
"""
import bisect
import statistics
import sys

EVENTS = ["GuestSubmit", "GuestSubmitDone", "GuestWaitBegin", "GuestWaitEnd", "DecodeBegin", "DecodeEnd",
          "PipeTask", "VulkanSubmit", "GpuDone", "FrameRetired", "DecoderIdleBegin", "DecoderIdleEnd",
          "StageBWait"]
E = {name: i for i, name in enumerate(EVENTS)}


def read(path):
    entries = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 4:
                continue
            ns, event, arg, arg2 = (int(p) for p in parts)
            if ns == 0:
                continue  # never written (the buffer was not full)
            entries.append((ns, event, arg, arg2))
    entries.sort(key=lambda e: e[0])
    return entries


def spans(entries, begin, end):
    """[start, stop) intervals between a begin event and the next end event."""
    out, start = [], None
    for ns, event, _, _ in entries:
        if event == begin and start is None:
            start = ns
        elif event == end and start is not None:
            out.append((start, ns))
            start = None
    return out


class Intervals:
    """Sorted, merged intervals (overlapping ones joined, so no share can pass 100%), looked up by
    bisection: the report stays linear in the file's length."""

    def __init__(self, intervals):
        merged = []
        for start, stop in sorted(intervals):
            if merged and start <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], stop)
            else:
                merged.append([start, stop])
        self.starts = [start for start, _ in merged]
        self.stops = [stop for _, stop in merged]

    def overlap(self, a, b):
        total, i = 0, bisect.bisect_right(self.stops, a)
        while i < len(self.starts) and self.starts[i] < b:
            total += min(self.stops[i], b) - max(self.starts[i], a)
            i += 1
        return total

    def first_start(self, at_least):
        i = bisect.bisect_left(self.starts, at_least)
        return self.starts[i] if i < len(self.starts) else None


def submissions(entries):
    """Each GPU tick's submission time: the rasterizer's (VulkanSubmit, arg) and the presenter's frame
    (PipeTask 5, arg2). Ticks are 32-bit in the file: compared as they are, which holds within a run."""
    out = {}
    for ns, event, arg, arg2 in entries:
        if event == E["VulkanSubmit"]:
            out[arg] = ns
        elif event == E["PipeTask"] and arg == 5:
            out[arg2] = ns
    return out


def completions(entries, ticks):
    """Each tick's completion: the first GpuDone that reached it (the GPU's progress only grows)."""
    done, i = {}, 0
    for ns, event, arg, _ in entries:
        if event != E["GpuDone"]:
            continue
        while i < len(ticks) and ticks[i] <= arg:
            done[ticks[i]] = ns
            i += 1
    return done


def gpu_idle_intervals(submitted, done):
    """The GPU had nothing to do: a tick submitted after the one before it was done. Also returns how
    many ticks were submitted while earlier work was still on the GPU."""
    out, queued = [], 0
    for tick in sorted(submitted):
        before = done.get(tick - 1)
        if before is None:
            continue
        if submitted[tick] > before:
            out.append((before, submitted[tick]))
        else:
            queued += 1
    return out, queued


def ms(ns):
    return ns / 1e6


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    limit = int(argv[argv.index("--frames") + 1]) if "--frames" in argv else 0
    entries = read(argv[1])
    if not entries:
        print("no events")
        return 1
    ends = [(ns, arg) for ns, event, arg, _ in entries if event == E["GuestSubmitDone"]]
    retired = {arg: ns for ns, event, arg, _ in entries if event == E["FrameRetired"]}
    waits = Intervals(spans(entries, E["GuestWaitBegin"], E["GuestWaitEnd"]))
    decoder_idle = Intervals(spans(entries, E["DecoderIdleBegin"], E["DecoderIdleEnd"]))
    stage_b_waits = Intervals((ns - arg * 1000, ns) for ns, event, arg, _ in entries if event == E["StageBWait"])
    submitted = submissions(entries)
    ticks = sorted(submitted)
    idle, queued = gpu_idle_intervals(submitted, completions(entries, ticks))
    gpu_idle = Intervals(idle)
    submits = sorted(submitted.values())
    presents = [ns for ns, event, arg, _ in entries if event == E["PipeTask"] and arg == 5]  # sorted

    rows, ways = [], []
    for (a, frame), (b, _) in zip(ends, ends[1:]):
        rows.append({
            "frame": b - a,
            "game wait": waits.overlap(a, b),
            "stage A idle": decoder_idle.overlap(a, b),
            "stage B waits": stage_b_waits.overlap(a, b),
            "GPU idle": gpu_idle.overlap(a, b),
            "submits": bisect.bisect_left(submits, b) - bisect.bisect_left(submits, a),
        })
        # The frame's way after its end was submitted.
        done = retired.get(frame)
        if done is None or done < a:
            continue
        a_done = decoder_idle.first_start(a)
        i = bisect.bisect_left(presents, a)
        presented = presents[i] if i < len(presents) else None
        if a_done is None or presented is None or presented > done:
            continue
        a_done = min(a_done, presented)
        ways.append({
            "stage A": a_done - a,
            "to the presenter's submission": presented - a_done,
            "the GPU's queue and work": done - presented,
            "in all": done - a,
        })
    if limit:
        rows, ways = rows[:limit], ways[:limit]
    if not rows:
        print("fewer than two frame ends")
        return 1

    span = ms(entries[-1][0] - entries[0][0])
    print(f"{len(entries)} events over {span:.0f} ms, {len(rows)} frames; {len(ticks)} GPU submissions, "
          f"{queued / max(1, len(ticks) - 1):.0%} of them queued behind earlier work on the GPU")
    print("Per frame, median (mean) in ms:")
    for key in rows[0]:
        values = [r[key] for r in rows]
        scale = (lambda v: v) if key == "submits" else ms
        print(f"  {key:16} {scale(statistics.median(values)):8.3f} ({scale(statistics.mean(values)):.3f})")
    frame = statistics.median(r["frame"] for r in rows)
    print(f"  shares of the median frame: game wait {statistics.median(r['game wait'] for r in rows) / frame:.0%}, "
          f"stage A idle {statistics.median(r['stage A idle'] for r in rows) / frame:.0%}, "
          f"stage B waits {statistics.median(r['stage B waits'] for r in rows) / frame:.0%}, "
          f"GPU idle {statistics.median(r['GPU idle'] for r in rows) / frame:.0%}")
    if ways:
        print(f"A frame's way from the game's frame-end submission to its retirement, median (mean) in ms, "
              f"{len(ways)} frames:")
        for key in ways[0]:
            values = [w[key] for w in ways]
            print(f"  {key:30} {ms(statistics.median(values)):8.3f} ({ms(statistics.mean(values)):.3f})")
    else:
        print("No frame's way could be followed (no stage A idle or presenter submission after its end).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
