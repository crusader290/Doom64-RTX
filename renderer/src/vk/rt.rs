//! Ray traced world renderer (VK_KHR_ray_query). Work in progress.
use super::VkBackend;
use crate::ffi::*;
use ash::vk;

pub struct RayTracer;

impl RayTracer {
    pub unsafe fn new(_be: &mut VkBackend) -> Result<Self, String> {
        Err("not implemented yet".into())
    }
    pub unsafe fn record(&mut self, _be: &mut VkBackend, _cmd: vk::CommandBuffer, _frame: &D64GfxFrame, _w: u32, _h: u32) -> Result<(), String> {
        Err("not implemented yet".into())
    }
    pub unsafe fn composite(&self, _d: &ash::Device, _cmd: vk::CommandBuffer, _vp: vk::Viewport) {}
    pub fn texture_created(&mut self, _id: u32, _w: u32, _h: u32, _rgba: &[u8]) {}
    pub fn texture_destroyed(&mut self, _id: u32) {}
    pub unsafe fn destroy(&mut self, _be: &mut VkBackend) {}
}
