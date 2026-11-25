// SPDX-License-Identifier: GPL-3.0-or-later
// 0.4.6 beta 16: the engine's FFmpeg (packaging/build-ffmpeg.sh, static and trimmed) plays what the movie
// player (gpu/shadps4/core/libraries/avplayer) accepts: the MP4 family (.mp4, .m4v, .mov, .m4a) with
// H.264 (with B-frames), HEVC (8- and 10-bit) and AAC (stereo and 5.1). Each file is opened by its path
// and through custom I/O (the game's file callbacks), its streams are decoded on threads of their own at
// once (the player's video and audio threads), video converted to NV12 (SWS_FAST_BILINEAR) and audio to
// 16-bit PCM, as the player does; every frame must come out. The movies (tests/data/movies, about
// 90 KB) are synthetic (FFmpeg's test pattern and sine, encoded with libx264, libx265 and its AAC).
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

struct Io {
    FILE* f;
};
static int ReadPacket(void* opaque, uint8_t* buf, int size) {
    auto* io = static_cast<Io*>(opaque);
    const size_t n = fread(buf, 1, size, io->f);
    return n == 0 ? AVERROR_EOF : int(n);
}
static int64_t Seek(void* opaque, int64_t offset, int whence) {
    auto* io = static_cast<Io*>(opaque);
    if (whence == AVSEEK_SIZE) {
        const long at = ftell(io->f);
        fseek(io->f, 0, SEEK_END);
        const long size = ftell(io->f);
        fseek(io->f, at, SEEK_SET);
        return size;
    }
    return fseek(io->f, long(offset), whence & ~AVSEEK_FORCE) == 0 ? ftell(io->f) : -1;
}

static int Decode(AVFormatContext* fmt, int index, const std::vector<AVPacket*>& packets, long* frames) {
    AVStream* stream = fmt->streams[index];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) return -1;
    AVCodecContext* ctx = avcodec_alloc_context3(decoder);
    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0) return -2;
    ctx->pkt_timebase = stream->time_base;
    if (avcodec_open2(ctx, decoder, nullptr) < 0) return -2;
    AVFrame* frame = av_frame_alloc();
    AVFrame* out = av_frame_alloc();
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    int bad = 0;
    auto drain = [&]() {
        while (avcodec_receive_frame(ctx, frame) == 0) {
            ++*frames;
            if (ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                if (!sws) {
                    sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                                         frame->height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                    out->format = AV_PIX_FMT_NV12;
                    out->width = frame->width;
                    out->height = frame->height;
                    if (!sws || av_frame_get_buffer(out, 0) < 0) bad = 1;
                }
                if (sws && sws_scale(sws, frame->data, frame->linesize, 0, frame->height, out->data, out->linesize) != frame->height) bad = 1;
            } else {
                av_frame_unref(out);
                out->format = AV_SAMPLE_FMT_S16;
                out->sample_rate = frame->sample_rate;
                av_channel_layout_copy(&out->ch_layout, &frame->ch_layout);
                if (!swr) {
                    if (swr_alloc_set_opts2(&swr, &frame->ch_layout, AV_SAMPLE_FMT_S16, frame->sample_rate, &frame->ch_layout,
                                            AVSampleFormat(frame->format), frame->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0) bad = 1;
                }
                if (swr && swr_convert_frame(swr, out, frame) < 0) bad = 1;
            }
            av_frame_unref(frame);
        }
    };
    for (AVPacket* p : packets) {
        if (avcodec_send_packet(ctx, p) < 0) bad = 1;
        drain();
    }
    avcodec_send_packet(ctx, nullptr);
    drain();
    sws_freeContext(sws);
    swr_free(&swr);
    av_frame_free(&frame);
    av_frame_free(&out);
    avcodec_free_context(&ctx);
    return bad ? -3 : 0;
}

static int Run(const char* path, bool custom, const char* expect) {
    AVFormatContext* fmt = avformat_alloc_context();
    Io io{};
    AVIOContext* avio = nullptr;
    int err;
    if (custom) {
        io.f = fopen(path, "rb");
        if (!io.f) {
            std::printf("FAIL %s: cannot be read\n", path);
            return 1;
        }
        auto* buffer = static_cast<unsigned char*>(av_malloc(64 * 1024));
        avio = avio_alloc_context(buffer, 64 * 1024, 0, &io, ReadPacket, nullptr, Seek);
        fmt->pb = avio;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        err = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    } else {
        err = avformat_open_input(&fmt, path, nullptr, nullptr);
    }
    if (err < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
        std::printf("FAIL %s (%s): cannot open (%d)\n", path, custom ? "custom I/O" : "path", err);
        return 1;
    }
    std::vector<std::vector<AVPacket*>> packets(fmt->nb_streams);
    AVPacket* p = av_packet_alloc();
    while (av_read_frame(fmt, p) >= 0) {
        packets[p->stream_index].push_back(av_packet_clone(p));
        av_packet_unref(p);
    }
    av_packet_free(&p);
    std::vector<long> frames(fmt->nb_streams, 0);
    std::vector<int> results(fmt->nb_streams, 0);
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        threads.emplace_back([&, i] { results[i] = Decode(fmt, int(i), packets[i], &frames[i]); });
    }
    for (auto& t : threads) t.join();
    std::string got;
    int failed = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        got += std::string(i ? " " : "") + avcodec_get_name(fmt->streams[i]->codecpar->codec_id) + "," + std::to_string(frames[i]);
        failed |= results[i] != 0;
        for (AVPacket* q : packets[i]) av_packet_free(&q);
    }
    const bool ok = !failed && got == expect;
    std::printf("%s %s, %s: %s%s%s\n", ok ? "ok  " : "FAIL", path, custom ? "custom I/O" : "path", got.c_str(),
                ok ? "" : " (expected ", ok ? "" : (std::string(expect) + ")").c_str());
    avformat_close_input(&fmt);
    if (avio) {
        av_freep(&avio->buffer);
        avio_context_free(&avio);
        fclose(io.f);
    }
    return ok ? 0 : 1;
}

#ifndef BB_MOVIE_DIR
#define BB_MOVIE_DIR "tests/data/movies"
#endif

int main(int argc, char** argv) {
    // Each movie and its streams' frames, as a full FFmpeg counts them (ffprobe -count_frames).
    static const char* const kMovies[][2] = {
        {"h264-aac.mp4", "h264,30 aac,47"},     {"h264-bframes.m4v", "h264,30 aac,47"},
        {"hevc-aac51.mp4", "hevc,30 aac,44"},   {"hevc10.mov", "hevc,24"},
        {"audio.m4a", "aac,47"},
    };
    const std::string dir = argc > 1 ? argv[1] : BB_MOVIE_DIR;
    av_log_set_level(AV_LOG_ERROR);
    int failed = 0;
    for (const auto& [name, expect] : kMovies) {
        const std::string path = dir + "/" + name;
        failed += Run(path.c_str(), false, expect);
        failed += Run(path.c_str(), true, expect);
    }
    std::printf("%s (FFmpeg %s)\n", failed ? "movie-decode-test: failures" : "movie-decode-test: ok", av_version_info());
    return failed ? 1 : 0;
}
