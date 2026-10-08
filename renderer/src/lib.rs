//! Doom64-RTX renderer.
//!
//! The C side interprets the game's N64 display lists into fully resolved
//! draw commands (see src/gfx/d64gfx.h); this crate draws them with either
//! Vulkan 1.1+ via `ash` (raster, plus an optional ray traced world path)
//! or an OpenGL 3.3 fallback via `glow`.
//!
//! GPLv3, part of the Doom64-RTX PC port.

mod ffi;
mod gl_backend;
mod shaders;
mod vk;

use ffi::*;
use std::ffi::{c_char, CStr};
use std::sync::Mutex;

/// Common interface of the two backends.
pub(crate) trait Backend {
    fn texture_create(&mut self, w: u32, h: u32, rgba: &[u8]) -> u32;
    fn texture_destroy(&mut self, id: u32);
    fn render(&mut self, frame: &D64GfxFrame);
    fn set_raytracing(&mut self, on: bool);
    fn set_vsync(&mut self, on: bool);
    fn rt_supported(&self) -> bool;
    fn rt_enabled(&self) -> bool;
    fn device_name(&self) -> String;
    fn backend_name(&self) -> &'static str;
    fn read_frame(&mut self, dst: &mut [u8], w: u32, h: u32);
    fn set_rt_params(&mut self, _p: RtParams) {}
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct RtParams {
    pub spp: i32,
    pub bounces: i32,
    pub denoise: bool,
    pub light_scale: f32,
    pub sun: bool,
}

impl Default for RtParams {
    fn default() -> Self {
        RtParams { spp: 1, bounces: 1, denoise: true, light_scale: 1.0, sun: true }
    }
}

struct State {
    backend: Box<dyn Backend>,
}

// The game is single threaded; the mutex only satisfies Rust's rules for
// statics.
unsafe impl Send for State {}

static STATE: Mutex<Option<State>> = Mutex::new(None);

fn with_backend<R>(f: impl FnOnce(&mut dyn Backend) -> R) -> Option<R> {
    let mut g = STATE.lock().ok()?;
    g.as_mut().map(|s| f(s.backend.as_mut()))
}

fn write_err(err: *mut c_char, errlen: u32, msg: &str) {
    if err.is_null() || errlen == 0 {
        return;
    }
    let buf = unsafe { std::slice::from_raw_parts_mut(err, errlen as usize) };
    write_cstr(buf, msg);
}

/// # Safety
/// `info` must point to a valid D64GfxInitInfo; `err` to `errlen` bytes.
#[no_mangle]
pub unsafe extern "C" fn d64gfx_init(info: *const D64GfxInitInfo, err: *mut c_char, errlen: u32) -> i32 {
    if info.is_null() {
        write_err(err, errlen, "null init info");
        return 0;
    }
    let info = &*info;
    if info.api_version != D64GFX_API_VERSION {
        write_err(err, errlen, "d64gfx API version mismatch between game and renderer");
        return 0;
    }
    if let Ok(mut g) = STATE.lock() {
        *g = None;
    }
    let window = Window(info.window);
    let result: Result<Box<dyn Backend>, String> = match info.backend {
        BACKEND_VULKAN => vk::VkBackend::new(info, window).map(|b| Box::new(b) as Box<dyn Backend>),
        _ => gl_backend::GlBackend::new(info, window).map(|b| Box::new(b) as Box<dyn Backend>),
    };
    match result {
        Ok(backend) => {
            if let Ok(mut g) = STATE.lock() {
                *g = Some(State { backend });
            }
            1
        }
        Err(e) => {
            window.log(2, &e);
            write_err(err, errlen, &e);
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn d64gfx_shutdown() {
    if let Ok(mut g) = STATE.lock() {
        *g = None;
    }
}

/// # Safety
/// `rgba8` must point to `w * h * 4` bytes.
#[no_mangle]
pub unsafe extern "C" fn d64gfx_texture_create(w: u32, h: u32, rgba8: *const u8) -> u32 {
    if rgba8.is_null() || w == 0 || h == 0 {
        return 0;
    }
    let data = std::slice::from_raw_parts(rgba8, (w * h * 4) as usize);
    with_backend(|b| b.texture_create(w, h, data)).unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn d64gfx_texture_destroy(id: u32) {
    with_backend(|b| b.texture_destroy(id));
}

/// # Safety
/// `frame` must point to a valid frame whose arrays live for the call.
#[no_mangle]
pub unsafe extern "C" fn d64gfx_render_frame(frame: *const D64GfxFrame) {
    if frame.is_null() {
        return;
    }
    let frame = &*frame;
    with_backend(|b| b.render(frame));
}

#[no_mangle]
pub extern "C" fn d64gfx_set_raytracing(enabled: i32) {
    with_backend(|b| b.set_raytracing(enabled != 0));
}

#[no_mangle]
pub extern "C" fn d64gfx_set_vsync(enabled: i32) {
    with_backend(|b| b.set_vsync(enabled != 0));
}

/// # Safety
/// `out` must be valid for writes.
#[no_mangle]
pub unsafe extern "C" fn d64gfx_status(out: *mut D64GfxStatus) {
    if out.is_null() {
        return;
    }
    let out = &mut *out;
    out.rt_supported = 0;
    out.rt_enabled = 0;
    write_cstr(&mut out.device_name, "none");
    write_cstr(&mut out.backend_name, "none");
    with_backend(|b| {
        out.rt_supported = b.rt_supported() as i32;
        out.rt_enabled = b.rt_enabled() as i32;
        write_cstr(&mut out.device_name, &b.device_name());
        write_cstr(&mut out.backend_name, b.backend_name());
    });
}

/// # Safety
/// `dst` must hold `w * h * 4` bytes.
#[no_mangle]
pub unsafe extern "C" fn d64gfx_read_frame(dst: *mut u8, w: u32, h: u32) {
    if dst.is_null() || w == 0 || h == 0 {
        return;
    }
    let out = std::slice::from_raw_parts_mut(dst, (w * h * 4) as usize);
    out.fill(0);
    with_backend(|b| b.read_frame(out, w, h));
}

#[no_mangle]
pub extern "C" fn d64gfx_set_rt_params(spp: i32, bounces: i32, denoise: i32, light_scale: f32, sun: i32) {
    let p = RtParams {
        spp: spp.clamp(1, 16),
        bounces: bounces.clamp(0, 4),
        denoise: denoise != 0,
        light_scale: if light_scale > 0.0 { light_scale } else { 1.0 },
        sun: sun != 0,
    };
    with_backend(|b| b.set_rt_params(p));
}

/// Helper for backends: C string from a callback.
pub(crate) fn cstr_to_string(p: *const c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }
}

/// Where the 320x240 game image lands inside the drawable (4:3 pillarbox).
pub(crate) fn game_viewport(w: u32, h: u32, stretch: bool) -> (i32, i32, u32, u32) {
    if stretch {
        return (0, 0, w, h);
    }
    let target_w = (h as u64 * 4 / 3) as u32;
    if target_w <= w {
        (((w - target_w) / 2) as i32, 0, target_w, h)
    } else {
        let target_h = (w as u64 * 3 / 4) as u32;
        (0, ((h - target_h) / 2) as i32, w, target_h)
    }
}

/// Nearest-neighbour rescale of an RGBA8 image (for read_frame).
pub(crate) fn rescale_rgba(src: &[u8], sw: u32, sh: u32, dst: &mut [u8], dw: u32, dh: u32, flip_y: bool) {
    if sw == 0 || sh == 0 {
        return;
    }
    for y in 0..dh {
        let mut sy = (y as u64 * sh as u64 / dh as u64) as u32;
        if flip_y {
            sy = sh - 1 - sy;
        }
        for x in 0..dw {
            let sx = (x as u64 * sw as u64 / dw as u64) as u32;
            let si = ((sy * sw + sx) * 4) as usize;
            let di = ((y * dw + x) * 4) as usize;
            dst[di..di + 4].copy_from_slice(&src[si..si + 4]);
        }
    }
}
