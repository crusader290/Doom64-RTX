#!/bin/sh
# Compiles the Vulkan GLSL shaders in renderer/shaders to SPIR-V.
# Requires glslangValidator (package glslang-tools / Vulkan SDK).
set -e
cd "$(dirname "$0")/../renderer/shaders"
mkdir -p spv
for s in vk_raster.vert vk_raster.frag vk_fullscreen.vert vk_composite.frag; do
    glslangValidator -V --target-env vulkan1.1 -o "spv/$s.spv" "$s"
done
for s in vk_rt.comp vk_denoise.comp; do
    glslangValidator -V --target-env vulkan1.2 -o "spv/$s.spv" "$s"
done
