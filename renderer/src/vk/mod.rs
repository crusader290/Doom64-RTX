//! Vulkan backend (work in progress).
use crate::ffi::*;
use crate::Backend;

pub struct VkBackend;

impl VkBackend {
    pub fn new(_info: &D64GfxInitInfo, _window: Window) -> Result<Self, String> {
        Err("Vulkan backend not built yet".into())
    }
}

impl Backend for VkBackend {
    fn texture_create(&mut self, _w: u32, _h: u32, _rgba: &[u8]) -> u32 { 0 }
    fn texture_destroy(&mut self, _id: u32) {}
    fn render(&mut self, _frame: &D64GfxFrame) {}
    fn set_raytracing(&mut self, _on: bool) {}
    fn set_vsync(&mut self, _on: bool) {}
    fn rt_supported(&self) -> bool { false }
    fn rt_enabled(&self) -> bool { false }
    fn device_name(&self) -> String { String::new() }
    fn backend_name(&self) -> &'static str { "Vulkan" }
    fn read_frame(&mut self, _dst: &mut [u8], _w: u32, _h: u32) {}
}
