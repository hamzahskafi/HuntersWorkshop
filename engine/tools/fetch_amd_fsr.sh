#!/usr/bin/env bash
# Downloads AMD's FSR SDK 2.4 runtime for Vulkan: AMD's signed amd_fidelityfx_loader_vk.dll and
# amd_fidelityfx_upscaler_vk.dll, with AMD's licence (docs/license.md), from AMD's public repository
# (github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v2.4.0). They are not part of this
# repository. The engine loads them from the package's engine/amd-fsr folder (BB_AMD_FSR_DIR
# overrides it; vk_amd_fsr.cpp) when upscaler=fsr. Windows only: AMD's SDK has no Linux build.
# AMD's licence: binary redistribution with its notice; no reverse engineering, decompilation or
# disassembly. Each file is checked against the SHA-256 of AMD's v2.4.0 release.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
version=v2.4.0
base="https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$version"
dest=${1:-amd-fsr}
mkdir -p "$dest"
files=(
    "Kits/FidelityFX/signedbin/amd_fidelityfx_loader_vk.dll 38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9"
    "Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_vk.dll 4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7"
    "docs/license.md 410f9c138e766e846acf2dc93db768198def0293a017d7e3296815b451688dbe"
)
for entry in "${files[@]}"; do
    file=${entry% *}
    sum=${entry##* }
    name=$(basename -- "$file")
    [[ $name == license.md ]] && name=LICENSE-AMD-FSR-SDK.md
    if [[ -s $dest/$name ]] && echo "$sum  $dest/$name" | sha256sum --check --status; then
        echo "AMD FSR: $dest/$name present ($version)"
        continue
    fi
    curl -fsSL --retry 3 -o "$dest/$name.part" "$base/$file"
    if ! echo "$sum  $dest/$name.part" | sha256sum --check --status; then
        rm -f -- "$dest/$name.part"
        echo "AMD FSR: $file from $version does not match its SHA-256; not installed" >&2
        exit 1
    fi
    mv "$dest/$name.part" "$dest/$name"
    echo "AMD FSR: downloaded $dest/$name ($version)"
done
echo "$version" > "$dest/AMD-FSR-SDK-VERSION.txt"
