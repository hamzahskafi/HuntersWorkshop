#!/usr/bin/env bash
# Downloads NVIDIA's DLSS Super Resolution model (nvngx_dlss.dll on Windows,
# libnvidia-ngx-dlss.so.<version> on Linux) from the public DLSS SDK (github.com/NVIDIA/DLSS),
# under NVIDIA's DLSS SDK license (LICENSE.txt there); it is not part of this repository.
# bbport loads it through the NVIDIA driver's NGX core when upscaler=dlss: it is searched next
# to the executable (out/ by default) and in BB_DLSS_DIR. Needs an NVIDIA RTX GPU.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
# 310.9.16 after Lantern 0.9.38 (EthanBlazkowicz's pull request). DLSS-VERSION.txt beside the files
# says which version they are: a different one (or none, from before it was written) is replaced.
version=v310.9.16
base="https://raw.githubusercontent.com/NVIDIA/DLSS/$version/lib"
dest=${1:-out}
mkdir -p "$dest"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) files=("Windows_x86_64/rel/nvngx_dlss.dll") ;;
    *) files=("Linux_x86_64/rel/libnvidia-ngx-dlss.so.${version#v}") ;;
esac
stamp="$dest/DLSS-VERSION.txt"
have=$(cat -- "$stamp" 2>/dev/null || true)
curl -fsSL --retry 3 -o "$dest/LICENSE-DLSS-SDK.txt" "https://raw.githubusercontent.com/NVIDIA/DLSS/$version/LICENSE.txt"
for file in "${files[@]}"; do
    name=$(basename -- "$file")
    if [[ -s $dest/$name && $have == "$version" ]]; then echo "DLSS: $dest/$name present ($version)"; continue; fi
    curl -fsSL --retry 3 -o "$dest/$name.part" "$base/$file"
    mv "$dest/$name.part" "$dest/$name"
    echo "DLSS: downloaded $dest/$name ($version)"
done
# Linux names the library by its version: an older one left beside it is removed.
for old in "$dest"/libnvidia-ngx-dlss.so.*; do
    if [[ -e $old && $(basename -- "$old") != "libnvidia-ngx-dlss.so.${version#v}" ]]; then rm -f -- "$old"; fi
done
echo "$version" > "$stamp"
