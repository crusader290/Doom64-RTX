//! Shader sources. GLSL for the OpenGL path is built at runtime from the
//! shared combiner; SPIR-V for Vulkan is compiled offline by
//! `tools/compile_shaders.sh` (glslangValidator) and checked in.

pub const COMBINER: &str = include_str!("../shaders/combiner.glsl");
pub const GL_RASTER_VERT: &str = include_str!("../shaders/gl_raster.vert");
const GL_RASTER_FRAG: &str = include_str!("../shaders/gl_raster.frag");

pub fn gl_raster_frag() -> String {
    GL_RASTER_FRAG.replace("//COMBINER//", COMBINER)
}

/// SPIR-V words from an embedded .spv file.
pub fn spv(bytes: &[u8]) -> Vec<u32> {
    assert!(bytes.len() % 4 == 0, "bad SPIR-V size");
    bytes
        .chunks_exact(4)
        .map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
        .collect()
}

pub const VK_RASTER_VERT: &[u8] = include_bytes!("../shaders/spv/vk_raster.vert.spv");
pub const VK_RASTER_FRAG: &[u8] = include_bytes!("../shaders/spv/vk_raster.frag.spv");
pub const VK_BLIT_VERT: &[u8] = include_bytes!("../shaders/spv/vk_fullscreen.vert.spv");
pub const VK_COMPOSITE_FRAG: &[u8] = include_bytes!("../shaders/spv/vk_composite.frag.spv");
pub const VK_RT_COMP: &[u8] = include_bytes!("../shaders/spv/vk_rt.comp.spv");
pub const VK_DENOISE_COMP: &[u8] = include_bytes!("../shaders/spv/vk_denoise.comp.spv");
