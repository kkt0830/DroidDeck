#!/usr/bin/env bash
set -euo pipefail
root=$(pwd)
source_commit=917b9b3b5ff62d7f17303c13fa9cc924c0f308a3
mesa_commit=5a07217f034b3e50d8c7c7794f97a2df1742613b
mkdir -p panvk-build
cd panvk-build
git init driver
git -C driver fetch --depth 1 https://github.com/zenithblue-oss/panvk-kbase-android.git "$source_commit"
git -C driver checkout --detach FETCH_HEAD
test "$(git -C driver rev-parse HEAD)" = "$source_commit"
git init mesa
git -C mesa fetch --depth 1 https://gitlab.freedesktop.org/mesa/mesa.git "$mesa_commit"
git -C mesa checkout --detach FETCH_HEAD
test "$(git -C mesa rev-parse HEAD)" = "$mesa_commit"
# This is the universal CSF series, including v12/G720; the g720 profile is incomplete.
bash driver/scripts/apply-patches.sh --profile g615-v11-csf --mesa "$PWD/mesa"
for patch in "$root"/tools/panvk/patches/*.patch; do
  git -C mesa apply --check "$patch"
  git -C mesa apply "$patch"
done
meson setup build mesa -Dbuildtype=release -Dplatforms=x11,wayland \
  -Dgallium-drivers= -Dvulkan-drivers=panfrost -Dpanfrost-kmds=kbase \
  -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false \
  -Dglx=disabled -Dgbm=disabled -Dzstd=disabled -Dlibunwind=disabled
ninja -C build -j 4
mkdir -p "$root/panvk-artifact"
cp build/src/panfrost/vulkan/libvulkan_panfrost.so "$root/panvk-artifact/"
cc -O2 -Wall -Wextra "$root/tools/panvk/probe.c" -o "$root/panvk-artifact/panvk-probe" $(pkg-config --cflags --libs vulkan wayland-client)
cp -r "$root/tools/panvk/patches" "$root/panvk-artifact/"
cp driver/NOTICE.md "$root/panvk-artifact/"
cp -r driver/LICENSES "$root/panvk-artifact/"
cd "$root/panvk-artifact"
cat > panfrost_icd.aarch64.json <<'ICD'
{"file_format_version":"1.0.0","ICD":{"library_path":"./libvulkan_panfrost.so","api_version":"1.3.0"}}
ICD
printf 'Experimental glibc Kbase ICD; no SM-X820 execution verified.\nDriver source: %s\nMesa source: %s\nProfile: g615-v11-csf (universal)\n' "$source_commit" "$mesa_commit" > BUILD.txt
readelf -h libvulkan_panfrost.so > elf-header.txt
readelf -d libvulkan_panfrost.so > dependencies.txt
readelf --version-info libvulkan_panfrost.so > symbol-versions.txt
grep -q AArch64 elf-header.txt
grep -q GLIBC_ symbol-versions.txt
sha256sum libvulkan_panfrost.so panvk-probe > SHA256SUMS
