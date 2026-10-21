#!/usr/bin/env bash
# Builds LGPL FFmpeg with the pinned Kazarov configuration and fetches checksum-pinned cxxbridge-cmd.
#   tools/ci/prebuilt-deps.sh linux-x64|macos-arm64
set -euo pipefail
release=https://github.com/Plarpoon/Kazarov/releases/download/deps-1
case ${1:-} in
    linux-x64)
        cxxbridge_sha=0fd5a8d3de5ec5471d1535e89e021b2cc7ec1709625e58f00206185ac2b30de6
        ffmpeg_dir=.kazarov/ffmpeg/linux-x64 ;;
    macos-arm64)
        cxxbridge_sha=a74d272155569f5d776f222457a7b3bbc7da6aece66e1ffc6eadb7516f8267b6
        ffmpeg_dir=.kazarov/ffmpeg/darwin-arm64 ;;
    *) echo "usage: $0 linux-x64|macos-arm64" >&2; exit 2 ;;
esac
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fetch() {
    curl -fsSL --retry 3 -o "$tmp/$1" "$release/$1"
    echo "$2  $tmp/$1" | shasum -a 256 -c -
}

python3 "$root/tools/kazarov/scripts/build/kz-ffmpeg" --out "$root/$ffmpeg_dir"

fetch "cxxbridge-1.0.205-$1.tar.gz" "$cxxbridge_sha"
bin=$root/.kazarov/bin
mkdir -p "$bin"
tar -xzf "$tmp/cxxbridge-1.0.205-$1.tar.gz" -C "$bin"
if [ -n "${GITHUB_PATH:-}" ]; then echo "$bin" >> "$GITHUB_PATH"; fi
