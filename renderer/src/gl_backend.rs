//! OpenGL 3.3 core fallback renderer (no ray tracing).
//!
//! Draws the interpreted N64 frame into an offscreen framebuffer at the
//! window's pixel size (the 4:3 game area centred in it), then blits to the
//! default framebuffer. The offscreen copy also serves `read_frame` for the
//! game's screen wipes and for screenshots.

use crate::ffi::*;
use crate::shaders;
use crate::{game_viewport, rescale_rgba, Backend};
use glow::HasContext;
use std::ffi::CString;

struct Fbo {
    fbo: glow::Framebuffer,
    color: glow::Texture,
    depth: glow::Renderbuffer,
    w: u32,
    h: u32,
}

struct Uniforms {
    tex0: Option<glow::UniformLocation>,
    tex1: Option<glow::UniformLocation>,
    cc: Option<glow::UniformLocation>,
    colors: Option<glow::UniformLocation>,
    flags: Option<glow::UniformLocation>,
    plod: Option<glow::UniformLocation>,
}

pub struct GlBackend {
    gl: glow::Context,
    window: Window,
    program: glow::Program,
    uniforms: Uniforms,
    vao: glow::VertexArray,
    vbo: glow::Buffer,
    samplers: Vec<glow::Sampler>,
    textures: Vec<Option<glow::Texture>>,
    free_ids: Vec<u32>,
    white: glow::Texture,
    fbo: Option<Fbo>,
    vsync: bool,
    version: String,
    game_rect: (i32, i32, u32, u32),
}

unsafe fn compile(gl: &glow::Context, vs: &str, fs: &str) -> Result<glow::Program, String> {
    let program = gl.create_program()?;
    let mut shaders_made = Vec::new();
    for (ty, src) in [(glow::VERTEX_SHADER, vs), (glow::FRAGMENT_SHADER, fs)] {
        let s = gl.create_shader(ty)?;
        gl.shader_source(s, src);
        gl.compile_shader(s);
        if !gl.get_shader_compile_status(s) {
            return Err(format!("GL shader compile failed: {}", gl.get_shader_info_log(s)));
        }
        gl.attach_shader(program, s);
        shaders_made.push(s);
    }
    gl.link_program(program);
    if !gl.get_program_link_status(program) {
        return Err(format!("GL program link failed: {}", gl.get_program_info_log(program)));
    }
    for s in shaders_made {
        gl.detach_shader(program, s);
        gl.delete_shader(s);
    }
    Ok(program)
}

fn wrap_gl(mode: u8) -> i32 {
    match mode {
        WRAP_MIRROR => glow::MIRRORED_REPEAT as i32,
        WRAP_CLAMP => glow::CLAMP_TO_EDGE as i32,
        _ => glow::REPEAT as i32,
    }
}

pub(crate) fn sampler_index(filter: bool, wrap: u8) -> usize {
    let s = (wrap & 3).min(2) as usize;
    let t = ((wrap >> 2) & 3).min(2) as usize;
    (filter as usize) * 9 + s * 3 + t
}

pub(crate) fn pack_rgba(c: [u8; 4]) -> u32 {
    ((c[0] as u32) << 24) | ((c[1] as u32) << 16) | ((c[2] as u32) << 8) | c[3] as u32
}

impl GlBackend {
    pub fn new(info: &D64GfxInitInfo, window: Window) -> Result<Self, String> {
        let getproc = window.0.gl_get_proc_address.ok_or("no GL loader callback")?;
        let user = window.0.user;
        let gl = unsafe {
            glow::Context::from_loader_function(|name| {
                let c = CString::new(name).unwrap();
                getproc(user, c.as_ptr()) as *const _
            })
        };
        unsafe {
            let version = gl.get_parameter_string(glow::VERSION);
            let renderer = gl.get_parameter_string(glow::RENDERER);
            let v = gl.version();
            if v.major < 3 || (v.major == 3 && v.minor < 3) {
                return Err(format!("OpenGL 3.3 required, got {version}"));
            }
            let fs_src = shaders::gl_raster_frag();
            let program = compile(&gl, shaders::GL_RASTER_VERT, &fs_src)?;
            let uniforms = Uniforms {
                tex0: gl.get_uniform_location(program, "u_tex0"),
                tex1: gl.get_uniform_location(program, "u_tex1"),
                cc: gl.get_uniform_location(program, "u_cc"),
                colors: gl.get_uniform_location(program, "u_colors"),
                flags: gl.get_uniform_location(program, "u_flags"),
                plod: gl.get_uniform_location(program, "u_plod"),
            };

            let vao = gl.create_vertex_array()?;
            let vbo = gl.create_buffer()?;
            gl.bind_vertex_array(Some(vao));
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(vbo));
            let stride = std::mem::size_of::<D64GfxVertex>() as i32;
            gl.enable_vertex_attrib_array(0);
            gl.vertex_attrib_pointer_f32(0, 4, glow::FLOAT, false, stride, 0);
            gl.enable_vertex_attrib_array(1);
            gl.vertex_attrib_pointer_f32(1, 2, glow::FLOAT, false, stride, 16);
            gl.enable_vertex_attrib_array(2);
            gl.vertex_attrib_pointer_f32(2, 4, glow::UNSIGNED_BYTE, true, stride, 24);
            gl.enable_vertex_attrib_array(3);
            gl.vertex_attrib_pointer_f32(3, 2, glow::FLOAT, false, stride, 40);

            let mut samplers = Vec::new();
            for filter in [false, true] {
                for s in 0..3u8 {
                    for t in 0..3u8 {
                        let smp = gl.create_sampler()?;
                        let f = if filter { glow::LINEAR } else { glow::NEAREST } as i32;
                        gl.sampler_parameter_i32(smp, glow::TEXTURE_MIN_FILTER, f);
                        gl.sampler_parameter_i32(smp, glow::TEXTURE_MAG_FILTER, f);
                        gl.sampler_parameter_i32(smp, glow::TEXTURE_WRAP_S, wrap_gl(s));
                        gl.sampler_parameter_i32(smp, glow::TEXTURE_WRAP_T, wrap_gl(t));
                        samplers.push(smp);
                    }
                }
            }

            let white = gl.create_texture()?;
            gl.bind_texture(glow::TEXTURE_2D, Some(white));
            gl.tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA8 as i32, 1, 1, 0, glow::RGBA,
                            glow::UNSIGNED_BYTE, Some(&[255, 255, 255, 255]));
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAX_LEVEL, 0);

            if let Some(f) = window.0.gl_set_swap_interval {
                f(window.0.user, if info.vsync != 0 { 1 } else { 0 });
            }

            window.log(0, &format!("OpenGL renderer: {renderer} ({version})"));
            Ok(GlBackend {
                gl,
                window,
                program,
                uniforms,
                vao,
                vbo,
                samplers,
                textures: vec![None],
                free_ids: Vec::new(),
                white,
                fbo: None,
                vsync: info.vsync != 0,
                version: format!("{renderer}"),
                game_rect: (0, 0, 1, 1),
            })
        }
    }

    unsafe fn ensure_fbo(&mut self, w: u32, h: u32) {
        if let Some(f) = &self.fbo {
            if f.w == w && f.h == h {
                return;
            }
        }
        let gl = &self.gl;
        if let Some(f) = self.fbo.take() {
            gl.delete_framebuffer(f.fbo);
            gl.delete_texture(f.color);
            gl.delete_renderbuffer(f.depth);
        }
        let fbo = gl.create_framebuffer().unwrap();
        let color = gl.create_texture().unwrap();
        gl.bind_texture(glow::TEXTURE_2D, Some(color));
        gl.tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA8 as i32, w as i32, h as i32, 0, glow::RGBA,
                        glow::UNSIGNED_BYTE, None);
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, glow::NEAREST as i32);
        gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, glow::NEAREST as i32);
        let depth = gl.create_renderbuffer().unwrap();
        gl.bind_renderbuffer(glow::RENDERBUFFER, Some(depth));
        gl.renderbuffer_storage(glow::RENDERBUFFER, glow::DEPTH_COMPONENT24, w as i32, h as i32);
        gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
        gl.framebuffer_texture_2d(glow::FRAMEBUFFER, glow::COLOR_ATTACHMENT0, glow::TEXTURE_2D, Some(color), 0);
        gl.framebuffer_renderbuffer(glow::FRAMEBUFFER, glow::DEPTH_ATTACHMENT, glow::RENDERBUFFER, Some(depth));
        if gl.check_framebuffer_status(glow::FRAMEBUFFER) != glow::FRAMEBUFFER_COMPLETE {
            self.window.log(2, "GL framebuffer incomplete");
        }
        self.fbo = Some(Fbo { fbo, color, depth, w, h });
    }

    fn tex(&self, id: u32) -> glow::Texture {
        self.textures.get(id as usize).copied().flatten().unwrap_or(self.white)
    }
}

impl Backend for GlBackend {
    fn texture_create(&mut self, w: u32, h: u32, rgba: &[u8]) -> u32 {
        unsafe {
            let gl = &self.gl;
            let t = match gl.create_texture() {
                Ok(t) => t,
                Err(_) => return 0,
            };
            gl.bind_texture(glow::TEXTURE_2D, Some(t));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
            gl.tex_image_2d(glow::TEXTURE_2D, 0, glow::RGBA8 as i32, w as i32, h as i32, 0, glow::RGBA,
                            glow::UNSIGNED_BYTE, Some(rgba));
            gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAX_LEVEL, 0);
            if let Some(id) = self.free_ids.pop() {
                self.textures[id as usize] = Some(t);
                id
            } else {
                self.textures.push(Some(t));
                (self.textures.len() - 1) as u32
            }
        }
    }

    fn texture_destroy(&mut self, id: u32) {
        if let Some(slot) = self.textures.get_mut(id as usize) {
            if let Some(t) = slot.take() {
                unsafe { self.gl.delete_texture(t) };
                self.free_ids.push(id);
            }
        }
    }

    fn render(&mut self, frame: &D64GfxFrame) {
        let (w, h) = self.window.drawable_size();
        unsafe {
            self.ensure_fbo(w, h);
            let rect = game_viewport(w, h, frame.vwidth());
            self.game_rect = rect;
            let gl = &self.gl;
            let fbo = self.fbo.as_ref().unwrap();
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo.fbo));
            gl.disable(glow::SCISSOR_TEST);
            gl.viewport(0, 0, w as i32, h as i32);
            gl.clear_color(0.0, 0.0, 0.0, 1.0);
            gl.clear(glow::COLOR_BUFFER_BIT | glow::DEPTH_BUFFER_BIT);

            let (vx, vy, vw, vh) = rect;
            // GL's origin is bottom-left
            let vy_gl = h as i32 - vy - vh as i32;
            gl.viewport(vx, vy_gl, vw as i32, vh as i32);
            gl.enable(glow::SCISSOR_TEST);
            gl.disable(glow::CULL_FACE);
            gl.disable(glow::DEPTH_TEST);

            let verts = frame.vertices();
            let bytes = std::slice::from_raw_parts(
                verts.as_ptr() as *const u8,
                std::mem::size_of_val(verts),
            );
            gl.bind_vertex_array(Some(self.vao));
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.vbo));
            gl.buffer_data_u8_slice(glow::ARRAY_BUFFER, bytes, glow::STREAM_DRAW);

            gl.use_program(Some(self.program));
            gl.uniform_1_i32(self.uniforms.tex0.as_ref(), 0);
            gl.uniform_1_i32(self.uniforms.tex1.as_ref(), 1);

            let gw = frame.vwidth();
            let sx = vw as f32 / gw;
            let sy = vh as f32 / 240.0;
            let mut blend_on = None;
            for c in frame.cmds() {
                if c.vertex_count == 0 {
                    continue;
                }
                let blend = c.flags & CMD_BLEND != 0;
                if blend_on != Some(blend) {
                    if blend {
                        gl.enable(glow::BLEND);
                        gl.blend_func(glow::SRC_ALPHA, glow::ONE_MINUS_SRC_ALPHA);
                    } else {
                        gl.disable(glow::BLEND);
                    }
                    blend_on = Some(blend);
                }
                // scissor in game pixels -> framebuffer pixels (bottom-left origin)
                let x0 = vx + (c.scissor[0].max(0) as f32 * sx) as i32;
                let x1 = vx + ((c.scissor[2] as f32).min(gw) * sx).ceil() as i32;
                let y0 = (c.scissor[1].max(0) as f32 * sy) as i32;
                let y1 = (c.scissor[3].min(240) as f32 * sy).ceil() as i32;
                gl.scissor(x0, vy_gl + vh as i32 - y1, (x1 - x0).max(0), (y1 - y0).max(0));

                let filter = c.flags & CMD_FILTER != 0;
                gl.active_texture(glow::TEXTURE0);
                gl.bind_texture(glow::TEXTURE_2D, Some(self.tex(c.tex[0])));
                gl.bind_sampler(0, Some(self.samplers[sampler_index(filter, c.wrap[0])]));
                gl.active_texture(glow::TEXTURE1);
                gl.bind_texture(glow::TEXTURE_2D, Some(self.tex(c.tex[1])));
                gl.bind_sampler(1, Some(self.samplers[sampler_index(filter, c.wrap[1])]));

                let cc = |i: usize| u32::from_le_bytes([c.cc[i], c.cc[i + 1], c.cc[i + 2], c.cc[i + 3]]);
                gl.uniform_4_u32(self.uniforms.cc.as_ref(), cc(0), cc(4), cc(8), cc(12));
                gl.uniform_4_u32(self.uniforms.colors.as_ref(), pack_rgba(c.prim), pack_rgba(c.env),
                                 pack_rgba(c.fog), pack_rgba(c.blend));
                gl.uniform_1_u32(self.uniforms.flags.as_ref(), c.flags);
                gl.uniform_1_f32(self.uniforms.plod.as_ref(), c.prim_lod_frac);
                gl.draw_arrays(glow::TRIANGLES, c.first_vertex as i32, c.vertex_count as i32);
            }
            gl.disable(glow::SCISSOR_TEST);
            gl.disable(glow::BLEND);

            // present
            gl.bind_framebuffer(glow::READ_FRAMEBUFFER, Some(fbo.fbo));
            gl.bind_framebuffer(glow::DRAW_FRAMEBUFFER, None);
            gl.viewport(0, 0, w as i32, h as i32);
            gl.clear_color(0.0, 0.0, 0.0, 1.0);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.blit_framebuffer(0, 0, w as i32, h as i32, 0, 0, w as i32, h as i32,
                                glow::COLOR_BUFFER_BIT, glow::NEAREST);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        }
        if let Some(swap) = self.window.0.gl_swap_window {
            unsafe { swap(self.window.0.user) };
        }
    }

    fn set_raytracing(&mut self, _on: bool) {}

    fn set_vsync(&mut self, on: bool) {
        self.vsync = on;
        if let Some(f) = self.window.0.gl_set_swap_interval {
            unsafe { f(self.window.0.user, on as i32) };
        }
    }

    fn rt_supported(&self) -> bool {
        false
    }
    fn rt_enabled(&self) -> bool {
        false
    }
    fn device_name(&self) -> String {
        self.version.clone()
    }
    fn backend_name(&self) -> &'static str {
        "OpenGL 3.3"
    }

    fn read_frame(&mut self, dst: &mut [u8], w: u32, h: u32) {
        let Some(fbo) = &self.fbo else { return };
        let (vx, vy, vw, vh) = self.game_rect;
        let mut buf = vec![0u8; (vw * vh * 4) as usize];
        unsafe {
            let gl = &self.gl;
            gl.bind_framebuffer(glow::READ_FRAMEBUFFER, Some(fbo.fbo));
            gl.pixel_store_i32(glow::PACK_ALIGNMENT, 1);
            let y_gl = fbo.h as i32 - vy - vh as i32;
            gl.read_pixels(vx, y_gl, vw as i32, vh as i32, glow::RGBA, glow::UNSIGNED_BYTE,
                           glow::PixelPackData::Slice(&mut buf));
            gl.bind_framebuffer(glow::READ_FRAMEBUFFER, None);
        }
        rescale_rgba(&buf, vw, vh, dst, w, h, true);
    }
}

impl Drop for GlBackend {
    fn drop(&mut self) {
        unsafe {
            let gl = &self.gl;
            for t in self.textures.drain(..).flatten() {
                gl.delete_texture(t);
            }
            for s in self.samplers.drain(..) {
                gl.delete_sampler(s);
            }
            gl.delete_texture(self.white);
            if let Some(f) = self.fbo.take() {
                gl.delete_framebuffer(f.fbo);
                gl.delete_texture(f.color);
                gl.delete_renderbuffer(f.depth);
            }
            gl.delete_buffer(self.vbo);
            gl.delete_vertex_array(self.vao);
            gl.delete_program(self.program);
        }
    }
}
