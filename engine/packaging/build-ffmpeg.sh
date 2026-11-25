#!/usr/bin/env bash
# Hunter's Workshop: the engine's FFmpeg, static and trimmed to what the game's movies need, the
# same for every package (0.4.6 beta 16; Windows took MSYS2's full shared FFmpeg until then: about
# 100 DLLs, 120 MB, for encoders, network and image libraries the engine never calls).
#
# The engine's one FFmpeg user is the movie player (gpu/shadps4/core/libraries/avplayer). It plays
# what the console's AvPlayer plays: MP4-family files (.mp4, .m4v, .m3d, .m4a, .mov; HLS is not
# implemented) with H.264 or HEVC video and AAC audio, read from a file or through the game's own
# file callbacks, decoded on its own threads (demuxer, video, audio), converted to NV12 (swscale)
# and 16-bit PCM (swresample). So: the MP4 demuxer, the three decoders and their parsers, the file
# protocol, and nothing autodetected from the system (no zlib, iconv, hardware decoders or network)
# except the platform's threads.
#
#   bash packaging/build-ffmpeg.sh            for this machine: Linux, or Windows in MSYS2 CLANG64
#   BB_FFMPEG_TARGET=x86_64-w64-windows-gnu bash packaging/build-ffmpeg.sh     a cross build
#
# Environment:
#   BB_FFMPEG_PREFIX   where it is installed (default: engine/deps/ffmpeg). gpu/CMakeLists.txt
#                      takes it from there, or from BB_FFMPEG_PREFIX (environment or CMake cache).
#   BB_FFMPEG_TARGET   a target triple for a cross build (x86_64-w64-windows-gnu,
#                      x86_64-w64-mingw32, x86_64-linux-gnu); empty: this machine.
#   BB_FFMPEG_SYSROOT  the target's sysroot, for a cross build with clang (--sysroot).
#   CC, AR, NM, RANLIB the tools: CC defaults to clang (given --target and --sysroot for a cross
#                      build when it is clang); AR, NM, RANLIB to llvm-ar, llvm-nm, llvm-ranlib for
#                      a cross build, FFmpeg's own choice otherwise.
#   BB_FFMPEG_CACHE    where the source tarball is kept (default: the prefix's folder, .cache).
#   BB_JOBS            parallel jobs (default: every processor).
# Needs: curl, sha256sum, tar with xz, make, nasm (FFmpeg's SIMD code: without it H.264 and HEVC
# decode several times slower, so its absence stops the build), and for MSYS2 diffutils (configure
# runs cmp).
set -euo pipefail

version=9.1
# The release tarball, its SHA-256 taken after checking FFmpeg's signature (ffmpeg-9.1.tar.xz.asc,
# FFmpeg release signing key FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8).
sha256=1a4d394ae63d190a526cd052eb4415c156b5c5e2df37efab41a7de0b8fde2433

die() { echo "build-ffmpeg: $*" >&2; exit 1; }
here=$(cd -- "$(dirname -- "$0")" && pwd)
engine=$(dirname -- "$here")
prefix=${BB_FFMPEG_PREFIX:-$engine/deps/ffmpeg}
target=${BB_FFMPEG_TARGET:-}
jobs=${BB_JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

if [[ -n $target ]]; then
    case $target in
    *-windows-*|*mingw*) os=windows ;;
    *-linux-*) os=linux ;;
    *) die "unknown target $target (x86_64-w64-windows-gnu, x86_64-w64-mingw32 or x86_64-linux-gnu)" ;;
    esac
    arch=${target%%-*}
else
    case $(uname -s) in
    MINGW*|MSYS*|CYGWIN*) os=windows ;;
    Linux) os=linux ;;
    *) die "this script builds on Linux and in MSYS2 CLANG64 (or cross-builds for them)" ;;
    esac
fi

# FFmpeg's makefiles cannot take a path with a space (a checkout under C:/Users/<name with a space>).
[[ $prefix != *' '* ]] || die "the prefix has a space in it ($prefix): set BB_FFMPEG_PREFIX to a path without one (CMake reads it too)"

cc=${CC:-clang}
clang=
if $cc --version 2>/dev/null | grep -qi clang; then clang=1; fi
for tool in curl sha256sum tar make nasm; do
    command -v "$tool" >/dev/null || die "needs $tool (README, Build)"
done

args=(--prefix="$prefix"
      --enable-static --disable-shared
      --disable-autodetect --disable-programs --disable-doc --disable-network
      --disable-everything --disable-avdevice --disable-avfilter
      --enable-demuxer=mov --enable-decoder=h264,hevc,aac --enable-parser=h264,hevc,aac
      --enable-protocol=file --enable-swscale --enable-swresample)
# Threads: --disable-autodetect also leaves them out, and without them FFmpeg's locks and one-time
# initialisations do nothing, while the movie player runs FFmpeg on three threads at once.
case $os in
linux) args+=(--enable-pthreads --enable-pic) ;; # PIC: linked into the GPU library (a shared object)
windows) args+=(--enable-w32threads) ;;
esac
# Debug information as the engine has it (build.sh: line tables only), so that crash offsets in
# FFmpeg still resolve to function and line; FFmpeg's default is full debug information.
if [[ -n $clang ]]; then
    args+=(--enable-debug=line-tables-only)
else
    args+=(--enable-debug=1)
fi
if [[ -n $target ]]; then
    if [[ -n $clang ]]; then
        cc="$cc --target=$target${BB_FFMPEG_SYSROOT:+ --sysroot=$BB_FFMPEG_SYSROOT}"
    fi
    args+=(--enable-cross-compile --arch="$arch" --target-os="$([[ $os == windows ]] && echo mingw32 || echo linux)"
           --ar="${AR:-llvm-ar}" --nm="${NM:-llvm-nm}" --ranlib="${RANLIB:-llvm-ranlib}")
else
    # MSYS2 CLANG64 may have LLVM's tools only by their llvm- names.
    if [[ -z ${AR:-} ]] && ! command -v ar >/dev/null && command -v llvm-ar >/dev/null; then
        AR=llvm-ar NM=${NM:-llvm-nm} RANLIB=${RANLIB:-llvm-ranlib}
    fi
    if [[ -n ${AR:-} ]]; then args+=(--ar="$AR"); fi
    if [[ -n ${NM:-} ]]; then args+=(--nm="$NM"); fi
    if [[ -n ${RANLIB:-} ]]; then args+=(--ranlib="$RANLIB"); fi
fi
args+=(--cc="$cc")

# Built once per version and set of options: a rerun with the same ones does nothing.
stamp=$prefix/share/hunters-workshop-ffmpeg.txt
want=$(printf '%s\n' "FFmpeg $version" "${args[@]}")
if [[ -f $stamp && $(cat "$stamp") == "$want" && -f $prefix/lib/libavcodec.a ]]; then
    echo "FFmpeg $version: up to date in $prefix"
    exit 0
fi

cache=${BB_FFMPEG_CACHE:-$(dirname -- "$prefix")/.cache}
mkdir -p "$cache" "$prefix"
tarball=$cache/ffmpeg-$version.tar.xz
if [[ ! -f $tarball ]] || ! echo "$sha256  $tarball" | sha256sum -c --status; then
    echo "FFmpeg $version: downloading"
    curl -fsSL --retry 3 -o "$tarball.part" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
    echo "$sha256  $tarball.part" | sha256sum -c --status || { rm -f "$tarball.part"; die "ffmpeg-$version.tar.xz: wrong checksum"; }
    mv -f "$tarball.part" "$tarball"
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar -xJf "$tarball" -C "$work"
cd "$work/ffmpeg-$version"
echo "FFmpeg $version: configuring for ${target:-this machine} ($os)"
if ! ./configure "${args[@]}" > "$work/configure.log" 2>&1; then
    tail -n 30 "$work/configure.log" >&2
    tail -n 30 ffbuild/config.log >&2 || true
    die "configure failed"
fi
grep -q '^#define HAVE_X86ASM 1' config.h || die "built without its SIMD code (nasm not used)"
grep -q '^#define HAVE_THREADS 1' config.h || die "built without threads"
echo "FFmpeg $version: building with $jobs jobs"
make -j"$jobs" > "$work/make.log" 2>&1 || { tail -n 40 "$work/make.log" >&2; die "build failed"; }
make install > "$work/install.log" 2>&1 || { tail -n 20 "$work/install.log" >&2; die "install failed"; }
mkdir -p "$(dirname -- "$stamp")"
printf '%s\n' "$want" > "$stamp"
echo "FFmpeg $version: installed in $prefix (pkg-config: $prefix/lib/pkgconfig)"
