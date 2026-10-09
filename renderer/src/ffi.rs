//! Rust mirror of src/gfx/d64gfx.h. Keep both in sync.
#![allow(non_camel_case_types, dead_code)]

use std::ffi::{c_char, c_int, c_void};

pub const D64GFX_API_VERSION: u32 = 3;

pub const BACKEND_VULKAN: u32 = 0;
pub const BACKEND_OPENGL: u32 = 1;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct D64GfxWindow {
    pub user: *mut c_void,
    pub vk_get_instance_proc_addr: Option<unsafe extern "C" fn(user: *mut c_void) -> *mut c_void>,
    pub vk_instance_extensions:
        Option<unsafe extern "C" fn(user: *mut c_void, count: *mut u32) -> *const *const c_char>,
    pub vk_create_surface:
        Option<unsafe extern "C" fn(user: *mut c_void, instance: u64, surface_out: *mut u64) -> c_int>,
    pub gl_get_proc_address:
        Option<unsafe extern "C" fn(user: *mut c_void, name: *const c_char) -> *mut c_void>,
    pub gl_swap_window: Option<unsafe extern "C" fn(user: *mut c_void)>,
    pub gl_set_swap_interval: Option<unsafe extern "C" fn(user: *mut c_void, interval: c_int)>,
    pub drawable_size: Option<unsafe extern "C" fn(user: *mut c_void, w: *mut c_int, h: *mut c_int)>,
    pub log: Option<unsafe extern "C" fn(user: *mut c_void, level: c_int, msg: *const c_char)>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct D64GfxInitInfo {
    pub api_version: u32,
    pub backend: u32,
    pub window: D64GfxWindow,
    pub vsync: i32,
    pub raytracing: i32,
    pub gpu_index: i32,
    pub validation: i32,
    pub game_width: u32,
    pub game_height: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D64GfxVertex {
    pub pos: [f32; 4],
    pub uv: [f32; 2],
    pub shade: [u8; 4],
    pub world: [f32; 3],
    pub uv1: [f32; 2],
}

pub const CMD_BLEND: u32 = 1 << 0;
pub const CMD_FOG: u32 = 1 << 1;
pub const CMD_ALPHA_CVG: u32 = 1 << 2;
pub const CMD_ALPHA_THRESH: u32 = 1 << 3;
pub const CMD_TWO_CYCLE: u32 = 1 << 4;
pub const CMD_FILTER: u32 = 1 << 5;
pub const CMD_LINES: u32 = 1 << 6;
pub const CMD_WORLD: u32 = 1 << 7;
pub const CMD_DEPTH_TEST: u32 = 1 << 8;
pub const CMD_DEPTH_WRITE: u32 = 1 << 9;
pub const CMD_SKY: u32 = 1 << 10;

pub const WRAP_REPEAT: u8 = 0;
pub const WRAP_MIRROR: u8 = 1;
pub const WRAP_CLAMP: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct D64GfxDrawCmd {
    pub first_vertex: u32,
    pub vertex_count: u32,
    pub tex: [u32; 2],
    pub cc: [u8; 16],
    pub flags: u32,
    pub wrap: [u8; 2],
    pub pad: [u8; 2],
    pub prim: [u8; 4],
    pub env: [u8; 4],
    pub fog: [u8; 4],
    pub blend: [u8; 4],
    pub prim_lod_frac: f32,
    pub scissor: [i16; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct D64GfxLight {
    pub pos: [f32; 3],
    pub radius: f32,
    pub color: [f32; 3],
    pub intensity: f32,
}

#[repr(C)]
pub struct D64GfxFrame {
    pub vertices: *const D64GfxVertex,
    pub vertex_count: u32,
    pub cmds: *const D64GfxDrawCmd,
    pub cmd_count: u32,
    pub has_camera: i32,
    pub view: [f32; 16],
    pub proj: [f32; 16],
    pub cam_pos: [f32; 3],
    pub world_first_cmd: u32,
    pub lights: *const D64GfxLight,
    pub light_count: u32,
    pub fog_color: [f32; 4],
    pub fog_near: f32,
    pub ambient_scale: f32,
    pub frame_index: u32,
    pub clear_color: [f32; 4],
    pub virtual_width: f32,
}

impl D64GfxFrame {
    /// Width of the game area in game units (320 = 4:3).
    pub fn vwidth(&self) -> f32 {
        if self.virtual_width >= 320.0 { self.virtual_width } else { 320.0 }
    }
    pub fn vertices(&self) -> &[D64GfxVertex] {
        if self.vertices.is_null() || self.vertex_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.vertices, self.vertex_count as usize) }
        }
    }
    pub fn cmds(&self) -> &[D64GfxDrawCmd] {
        if self.cmds.is_null() || self.cmd_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.cmds, self.cmd_count as usize) }
        }
    }
    pub fn lights(&self) -> &[D64GfxLight] {
        if self.lights.is_null() || self.light_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.lights, self.light_count as usize) }
        }
    }
}

#[repr(C)]
pub struct D64GfxStatus {
    pub rt_supported: i32,
    pub rt_enabled: i32,
    pub device_name: [c_char; 256],
    pub backend_name: [c_char; 64],
}

/// Thin wrapper for the C window callbacks.
#[derive(Clone, Copy)]
pub struct Window(pub D64GfxWindow);

unsafe impl Send for Window {}
// The callbacks are plain C functions; the game uses them from one thread.
unsafe impl Sync for Window {}

impl Window {
    pub fn log(&self, level: i32, msg: &str) {
        if let Some(f) = self.0.log {
            let c = std::ffi::CString::new(msg.replace('\0', " ")).unwrap_or_default();
            unsafe { f(self.0.user, level, c.as_ptr()) };
        } else {
            eprintln!("[d64gfx] {msg}");
        }
    }
    pub fn drawable_size(&self) -> (u32, u32) {
        let (mut w, mut h) = (0, 0);
        if let Some(f) = self.0.drawable_size {
            unsafe { f(self.0.user, &mut w, &mut h) };
        }
        (w.max(1) as u32, h.max(1) as u32)
    }
}

/// Copies a Rust string into a fixed C char buffer.
pub fn write_cstr(dst: &mut [c_char], s: &str) {
    let n = s.len().min(dst.len().saturating_sub(1));
    for (i, b) in s.as_bytes()[..n].iter().enumerate() {
        dst[i] = *b as c_char;
    }
    if !dst.is_empty() {
        dst[n] = 0;
    }
}
