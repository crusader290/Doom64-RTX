//! Ray traced world renderer (VK_KHR_acceleration_structure + VK_KHR_ray_query).
//!
//! Every frame the world triangles the interpreter tagged CMD_WORLD (minus
//! translucent ones, which stay rasterised) are packed into one BLAS with two
//! geometries: opaque triangles, and alpha tested ones (sprites, grates)
//! whose texture alpha is evaluated in the shader. A compute shader traces
//! primary rays, AO/bounce rays and light shadow rays (vk_rt.comp), a second
//! pass filters and resolves (vk_denoise.comp), and a fullscreen draw
//! composites the result into the raster pass at the world's place in draw
//! order (vk_composite.frag), writing depth for later translucent draws.

use super::mem::{barrier, Buffer, Image};
use super::{create_shader, vkerr, VkBackend, FRAMES};
use crate::ffi::*;
use crate::gl_backend::{pack_rgba, sampler_index};
use crate::shaders;
use ash::{khr, vk};
use std::ffi::CString;

const MAX_TEXTURES: u32 = 4096;

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RtVertex {
    pos: [f32; 3],
    uv: [f32; 2],
    color: u32,
    mat: u32,
    pad: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RtMaterial {
    cc: [u32; 4],
    colors: [u32; 4],
    flags: u32,
    plod: f32,
    tex: u32,
    smp: u32,
    /// material maps: orm, normal, emissive (u32::MAX = none), pad
    maps: [u32; 4],
    /// roughness, metallic (< 0 = from the ORM map), emissive multiplier, unused
    mparams: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RtLightGpu {
    pos_radius: [f32; 4],
    color_intensity: [f32; 4],
    direction_outer: [f32; 4],
    inner_pad: [f32; 4],
}
const _: () = assert!(std::mem::size_of::<RtLightGpu>() == 64);

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RtUniforms {
    view: [f32; 16],
    proj: [f32; 16],
    inv_view: [f32; 16],
    inv_proj: [f32; 16],
    prev_viewproj: [f32; 16],
    cam_pos: [f32; 4],
    fog_color: [f32; 4],
    dims: [u32; 4],
    counts: [u32; 4],
    params: [f32; 4],
    params2: [f32; 4],
    prev_cam: [f32; 4],
}

#[derive(Default)]
struct Accel {
    handle: vk::AccelerationStructureKHR,
    buffer: Buffer,
    address: vk::DeviceAddress,
}

#[derive(Default)]
struct RtFrame {
    verts: Buffer,
    mats: Buffer,
    lights: Buffer,
    ubo: Buffer,
    inst: Buffer,
    scratch: Buffer,
    blas: Accel,
    tlas: Accel,
    set_rt: vk::DescriptorSet,
    set_dn: vk::DescriptorSet,
    set_comp: vk::DescriptorSet,
    tex_mirror: Vec<vk::ImageView>,
}

struct RtImages {
    w: u32,
    h: u32,
    hist: [Image; 2],
    albedo: Image,
    nd: Image,
    depth: Image,
    color: Image,
    /// emissive + specular light, added after the denoised diffuse
    extra: Image,
    fresh: bool,
}

pub struct RayTracer {
    as_fn: khr::acceleration_structure::Device,
    scratch_align: u64,
    max_textures: u32,
    dsl_rt: vk::DescriptorSetLayout,
    dsl_dn: vk::DescriptorSetLayout,
    dsl_comp: vk::DescriptorSetLayout,
    pl_rt: vk::PipelineLayout,
    pl_dn: vk::PipelineLayout,
    pl_comp: vk::PipelineLayout,
    pipe_rt: vk::Pipeline,
    pipe_dn: vk::Pipeline,
    pipe_comp: vk::Pipeline,
    pool: vk::DescriptorPool,
    sampler: vk::Sampler,
    frames: Vec<RtFrame>,
    images: Option<RtImages>,
    parity: usize,
    prev_viewproj: [f32; 16],
    prev_cam: [f32; 3],
    history_valid: bool,
    counter: u32,
    cur_slot: usize,
}

// ---------------------------------------------------------------- math
fn mat_mul(a: &[f32; 16], b: &[f32; 16]) -> [f32; 16] {
    // column-major: r = a * b
    let mut r = [0.0; 16];
    for c in 0..4 {
        for row in 0..4 {
            r[c * 4 + row] = (0..4).map(|k| a[k * 4 + row] * b[c * 4 + k]).sum();
        }
    }
    r
}

fn mat_inv(m: &[f32; 16]) -> [f32; 16] {
    let a = m;
    let mut inv = [0.0f32; 16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    let det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if det.abs() < 1e-20 {
        return *m;
    }
    for v in inv.iter_mut() {
        *v /= det;
    }
    inv
}

fn as_bytes<T>(v: &[T]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(v.as_ptr() as *const u8, std::mem::size_of_val(v)) }
}

impl RayTracer {
    pub unsafe fn new(be: &mut VkBackend) -> Result<Self, String> {
        let core = &be.core;
        let d = &core.device;
        let as_fn = khr::acceleration_structure::Device::new(&core.instance, d);

        let mut as_props = vk::PhysicalDeviceAccelerationStructurePropertiesKHR::default();
        let mut di_props = vk::PhysicalDeviceDescriptorIndexingProperties::default();
        let mut props2 = vk::PhysicalDeviceProperties2::default().push_next(&mut as_props).push_next(&mut di_props);
        core.instance.get_physical_device_properties2(core.pdev, &mut props2);
        let limits = props2.properties.limits;
        let max_textures = MAX_TEXTURES
            .min(limits.max_per_stage_descriptor_sampled_images.saturating_sub(16))
            .min(limits.max_descriptor_set_sampled_images.saturating_sub(16));
        if max_textures < 512 {
            return Err(format!("device allows only {max_textures} sampled images per stage"));
        }
        let scratch_align = as_props.min_acceleration_structure_scratch_offset_alignment.max(1) as u64;

        // --- descriptor layouts -----------------------------------------
        let cs = vk::ShaderStageFlags::COMPUTE;
        let b = |i: u32, ty: vk::DescriptorType, n: u32| {
            vk::DescriptorSetLayoutBinding::default().binding(i).descriptor_type(ty).descriptor_count(n).stage_flags(cs)
        };
        let rt_bindings = [
            b(0, vk::DescriptorType::ACCELERATION_STRUCTURE_KHR, 1),
            b(1, vk::DescriptorType::STORAGE_BUFFER, 1),
            b(2, vk::DescriptorType::STORAGE_BUFFER, 1),
            b(3, vk::DescriptorType::STORAGE_BUFFER, 1),
            b(4, vk::DescriptorType::UNIFORM_BUFFER, 1),
            b(5, vk::DescriptorType::SAMPLED_IMAGE, max_textures),
            b(6, vk::DescriptorType::SAMPLER, 18),
            b(7, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(8, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(9, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(10, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(11, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(12, vk::DescriptorType::STORAGE_IMAGE, 1),
        ];
        let mut bflags = [vk::DescriptorBindingFlags::empty(); 13];
        bflags[5] = vk::DescriptorBindingFlags::PARTIALLY_BOUND;
        let mut flags_info = vk::DescriptorSetLayoutBindingFlagsCreateInfo::default().binding_flags(&bflags);
        let dsl_rt = d
            .create_descriptor_set_layout(
                &vk::DescriptorSetLayoutCreateInfo::default().bindings(&rt_bindings).push_next(&mut flags_info),
                None,
            )
            .map_err(vkerr("rt set layout"))?;
        let dn_bindings = [
            b(0, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(1, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(2, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(3, vk::DescriptorType::STORAGE_IMAGE, 1),
            b(4, vk::DescriptorType::UNIFORM_BUFFER, 1),
            b(5, vk::DescriptorType::STORAGE_IMAGE, 1),
        ];
        let dsl_dn = d
            .create_descriptor_set_layout(&vk::DescriptorSetLayoutCreateInfo::default().bindings(&dn_bindings), None)
            .map_err(vkerr("denoise set layout"))?;
        let comp_bindings = [
            vk::DescriptorSetLayoutBinding::default()
                .binding(0)
                .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
                .descriptor_count(1)
                .stage_flags(vk::ShaderStageFlags::FRAGMENT),
            vk::DescriptorSetLayoutBinding::default()
                .binding(1)
                .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
                .descriptor_count(1)
                .stage_flags(vk::ShaderStageFlags::FRAGMENT),
        ];
        let dsl_comp = d
            .create_descriptor_set_layout(&vk::DescriptorSetLayoutCreateInfo::default().bindings(&comp_bindings), None)
            .map_err(vkerr("composite set layout"))?;

        let mk_layout = |l: vk::DescriptorSetLayout| {
            let ls = [l];
            d.create_pipeline_layout(&vk::PipelineLayoutCreateInfo::default().set_layouts(&ls), None)
        };
        let pl_rt = mk_layout(dsl_rt).map_err(vkerr("rt layout"))?;
        let pl_dn = mk_layout(dsl_dn).map_err(vkerr("denoise layout"))?;
        let pl_comp = mk_layout(dsl_comp).map_err(vkerr("composite layout"))?;

        // --- pipelines -------------------------------------------------
        let main = CString::new("main").unwrap();
        let mk_compute = |code: &[u8], layout: vk::PipelineLayout| -> Result<vk::Pipeline, String> {
            let m = create_shader(d, code)?;
            let ci = vk::ComputePipelineCreateInfo::default()
                .stage(vk::PipelineShaderStageCreateInfo::default().stage(cs).module(m).name(&main))
                .layout(layout);
            let p = d
                .create_compute_pipelines(vk::PipelineCache::null(), &[ci], None)
                .map_err(|(_, e)| format!("compute pipeline: {e}"));
            d.destroy_shader_module(m, None);
            Ok(p?[0])
        };
        let pipe_rt = mk_compute(shaders::VK_RT_COMP, pl_rt)?;
        let pipe_dn = mk_compute(shaders::VK_DENOISE_COMP, pl_dn)?;
        let pipe_comp = Self::create_composite_pipeline(d, be.render_pass, pl_comp)?;

        // --- descriptor pool & sets --------------------------------------
        let n = FRAMES as u32;
        let sizes = [
            vk::DescriptorPoolSize { ty: vk::DescriptorType::ACCELERATION_STRUCTURE_KHR, descriptor_count: n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::STORAGE_BUFFER, descriptor_count: 3 * n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::UNIFORM_BUFFER, descriptor_count: 2 * n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::SAMPLED_IMAGE, descriptor_count: max_textures * n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::SAMPLER, descriptor_count: 18 * n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::STORAGE_IMAGE, descriptor_count: 11 * n },
            vk::DescriptorPoolSize { ty: vk::DescriptorType::COMBINED_IMAGE_SAMPLER, descriptor_count: 2 * n },
        ];
        let pool = d
            .create_descriptor_pool(&vk::DescriptorPoolCreateInfo::default().max_sets(3 * n).pool_sizes(&sizes), None)
            .map_err(vkerr("rt descriptor pool"))?;
        let sampler = d
            .create_sampler(
                &vk::SamplerCreateInfo::default()
                    .mag_filter(vk::Filter::NEAREST)
                    .min_filter(vk::Filter::NEAREST)
                    .address_mode_u(vk::SamplerAddressMode::CLAMP_TO_EDGE)
                    .address_mode_v(vk::SamplerAddressMode::CLAMP_TO_EDGE)
                    .address_mode_w(vk::SamplerAddressMode::CLAMP_TO_EDGE),
                None,
            )
            .map_err(vkerr("rt sampler"))?;

        let mut frames = Vec::new();
        for _ in 0..FRAMES {
            let layouts = [dsl_rt, dsl_dn, dsl_comp];
            let sets = d
                .allocate_descriptor_sets(&vk::DescriptorSetAllocateInfo::default().descriptor_pool(pool).set_layouts(&layouts))
                .map_err(vkerr("rt descriptor sets"))?;
            // the sampler array never changes
            let infos: Vec<vk::DescriptorImageInfo> =
                be.samplers.iter().map(|&s| vk::DescriptorImageInfo { sampler: s, ..Default::default() }).collect();
            let w = vk::WriteDescriptorSet::default()
                .dst_set(sets[0])
                .dst_binding(6)
                .descriptor_type(vk::DescriptorType::SAMPLER)
                .image_info(&infos);
            d.update_descriptor_sets(&[w], &[]);
            frames.push(RtFrame {
                set_rt: sets[0],
                set_dn: sets[1],
                set_comp: sets[2],
                tex_mirror: vec![vk::ImageView::null(); max_textures as usize],
                ..Default::default()
            });
        }

        be.window.log(0, &format!("ray tracing ready ({max_textures} texture slots)"));
        Ok(RayTracer {
            as_fn,
            scratch_align,
            max_textures,
            dsl_rt,
            dsl_dn,
            dsl_comp,
            pl_rt,
            pl_dn,
            pl_comp,
            pipe_rt,
            pipe_dn,
            pipe_comp,
            pool,
            sampler,
            frames,
            images: None,
            parity: 0,
            prev_viewproj: [0.0; 16],
            prev_cam: [0.0; 3],
            history_valid: false,
            counter: 0,
            cur_slot: 0,
        })
    }

    unsafe fn create_composite_pipeline(
        d: &ash::Device,
        render_pass: vk::RenderPass,
        layout: vk::PipelineLayout,
    ) -> Result<vk::Pipeline, String> {
        let vs = create_shader(d, shaders::VK_BLIT_VERT)?;
        let fs = create_shader(d, shaders::VK_COMPOSITE_FRAG)?;
        let main = CString::new("main").unwrap();
        let stages = [
            vk::PipelineShaderStageCreateInfo::default().stage(vk::ShaderStageFlags::VERTEX).module(vs).name(&main),
            vk::PipelineShaderStageCreateInfo::default().stage(vk::ShaderStageFlags::FRAGMENT).module(fs).name(&main),
        ];
        let vi = vk::PipelineVertexInputStateCreateInfo::default();
        let ia = vk::PipelineInputAssemblyStateCreateInfo::default().topology(vk::PrimitiveTopology::TRIANGLE_LIST);
        let vp = vk::PipelineViewportStateCreateInfo::default().viewport_count(1).scissor_count(1);
        let rs = vk::PipelineRasterizationStateCreateInfo::default()
            .polygon_mode(vk::PolygonMode::FILL)
            .cull_mode(vk::CullModeFlags::NONE)
            .line_width(1.0);
        let ms = vk::PipelineMultisampleStateCreateInfo::default().rasterization_samples(vk::SampleCountFlags::TYPE_1);
        let att = [vk::PipelineColorBlendAttachmentState::default()
            .blend_enable(false)
            .color_write_mask(vk::ColorComponentFlags::RGBA)];
        let cb = vk::PipelineColorBlendStateCreateInfo::default().attachments(&att);
        let ds = vk::PipelineDepthStencilStateCreateInfo::default()
            .depth_test_enable(true)
            .depth_write_enable(true)
            .depth_compare_op(vk::CompareOp::ALWAYS);
        let dyn_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
        let dy = vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dyn_states);
        let gp = vk::GraphicsPipelineCreateInfo::default()
            .stages(&stages)
            .vertex_input_state(&vi)
            .input_assembly_state(&ia)
            .viewport_state(&vp)
            .rasterization_state(&rs)
            .multisample_state(&ms)
            .depth_stencil_state(&ds)
            .color_blend_state(&cb)
            .dynamic_state(&dy)
            .layout(layout)
            .render_pass(render_pass)
            .subpass(0);
        let p = d
            .create_graphics_pipelines(vk::PipelineCache::null(), &[gp], None)
            .map_err(|(_, e)| format!("composite pipeline: {e}"));
        d.destroy_shader_module(vs, None);
        d.destroy_shader_module(fs, None);
        Ok(p?[0])
    }

    unsafe fn ensure_images(&mut self, be: &mut VkBackend, w: u32, h: u32) -> Result<(), String> {
        if let Some(im) = &self.images {
            if im.w == w && im.h == h {
                return Ok(());
            }
        }
        let d = be.core.device.clone();
        d.device_wait_idle().ok();
        if let Some(mut old) = self.images.take() {
            for img in old.hist.iter_mut().chain([&mut old.albedo, &mut old.nd, &mut old.depth, &mut old.color, &mut old.extra]) {
                img.destroy(&d, &mut be.core.alloc);
            }
        }
        let st = vk::ImageUsageFlags::STORAGE | vk::ImageUsageFlags::SAMPLED;
        let c = vk::ImageAspectFlags::COLOR;
        let mut mk = |f: vk::Format| Image::new(&d, &mut be.core.alloc, w, h, f, st, c);
        self.images = Some(RtImages {
            w,
            h,
            hist: [mk(vk::Format::R16G16B16A16_SFLOAT)?, mk(vk::Format::R16G16B16A16_SFLOAT)?],
            albedo: mk(vk::Format::R8G8B8A8_UNORM)?,
            nd: mk(vk::Format::R16G16B16A16_SFLOAT)?,
            depth: mk(vk::Format::R32_SFLOAT)?,
            color: mk(vk::Format::R8G8B8A8_UNORM)?,
            extra: mk(vk::Format::R16G16B16A16_SFLOAT)?,
            fresh: true,
        });
        self.history_valid = false;
        Ok(())
    }

    unsafe fn ensure_buffer(
        be: &mut VkBackend,
        buf: &mut Buffer,
        size: usize,
        usage: vk::BufferUsageFlags,
        host: bool,
    ) -> Result<(), String> {
        if (buf.size as usize) < size.max(64) {
            let d = be.core.device.clone();
            buf.destroy(&d, &mut be.core.alloc);
            *buf = Buffer::new(&d, &mut be.core.alloc, (size.max(64) as u64).next_power_of_two(), usage, host)?;
        }
        Ok(())
    }

    unsafe fn ensure_accel(
        &self,
        be: &mut VkBackend,
        acc: &mut Accel,
        size: u64,
        ty: vk::AccelerationStructureTypeKHR,
    ) -> Result<(), String> {
        if acc.handle != vk::AccelerationStructureKHR::null() && acc.buffer.size >= size {
            return Ok(());
        }
        let d = be.core.device.clone();
        if acc.handle != vk::AccelerationStructureKHR::null() {
            self.as_fn.destroy_acceleration_structure(acc.handle, None);
            acc.buffer.destroy(&d, &mut be.core.alloc);
        }
        let size = size.next_power_of_two();
        acc.buffer = Buffer::new(
            &d,
            &mut be.core.alloc,
            size,
            vk::BufferUsageFlags::ACCELERATION_STRUCTURE_STORAGE_KHR | vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS,
            false,
        )?;
        acc.handle = self
            .as_fn
            .create_acceleration_structure(
                &vk::AccelerationStructureCreateInfoKHR::default().buffer(acc.buffer.buffer).size(size).ty(ty),
                None,
            )
            .map_err(vkerr("create acceleration structure"))?;
        acc.address = self
            .as_fn
            .get_acceleration_structure_device_address(
                &vk::AccelerationStructureDeviceAddressInfoKHR::default().acceleration_structure(acc.handle),
            );
        Ok(())
    }

    /// Records the RT passes for this frame. Returns Ok(false) when the frame
    /// has nothing to trace (caller then rasterises everything).
    pub unsafe fn record(
        &mut self,
        be: &mut VkBackend,
        cmd: vk::CommandBuffer,
        frame: &D64GfxFrame,
        w: u32,
        h: u32,
    ) -> Result<bool, String> {
        let fi = be.frame_idx;
        self.cur_slot = fi;
        let d = be.core.device.clone();

        // ---- gather world geometry ----------------------------------------
        let verts_in = frame.vertices();
        let mut opaque: Vec<RtVertex> = Vec::new();
        let mut alpha: Vec<RtVertex> = Vec::new();
        let mut mats: Vec<RtMaterial> = Vec::new();
        let mut fog = [0.0f32; 4];
        let mut have_fog = false;
        for c in frame.cmds() {
            if c.flags & CMD_WORLD == 0 || c.flags & CMD_BLEND != 0 || c.vertex_count < 3 {
                continue;
            }
            let mi = mats.len() as u32;
            let tex = if c.tex[0] != 0 && (c.tex[0] as usize) < be.textures.len() && be.textures[c.tex[0] as usize].is_some() {
                c.tex[0]
            } else {
                be.white
            };
            let ccw = |i: usize| u32::from_le_bytes([c.cc[i], c.cc[i + 1], c.cc[i + 2], c.cc[i + 3]]);
            mats.push(RtMaterial {
                cc: [ccw(0), ccw(4), ccw(8), ccw(12)],
                colors: [pack_rgba(c.prim), pack_rgba(c.env), pack_rgba(c.fog), pack_rgba(c.blend)],
                flags: c.flags,
                plod: c.prim_lod_frac,
                tex: if tex < self.max_textures { tex } else { be.white },
                smp: sampler_index(c.flags & CMD_FILTER != 0, c.wrap[0]) as u32,
                maps: [u32::MAX; 4],
                mparams: [-1.0, -1.0, 1.0, 0.0],
            });
            if let Some(Some(t)) = be.textures.get(tex as usize) {
                if let Some((ids, prm)) = t.mat {
                    let last = mats.len() - 1;
                    for k in 0..3 {
                        let id = ids[k];
                        if id != 0 && id < self.max_textures && matches!(be.textures.get(id as usize), Some(Some(_))) {
                            mats[last].maps[k] = id;
                        }
                    }
                    mats[last].mparams = prm;
                }
            }
            if !have_fog && c.flags & CMD_FOG != 0 {
                fog = [c.fog[0] as f32 / 255.0, c.fog[1] as f32 / 255.0, c.fog[2] as f32 / 255.0, 1.0];
                have_fog = true;
            }
            let dst = if c.flags & (CMD_ALPHA_CVG | CMD_ALPHA_THRESH) != 0 { &mut alpha } else { &mut opaque };
            let first = c.first_vertex as usize;
            let count = (c.vertex_count as usize / 3) * 3;
            for v in &verts_in[first..(first + count).min(verts_in.len())] {
                let s = v.shade;
                let color = ((s[0] as u32) << 24) | ((s[1] as u32) << 16) | ((s[2] as u32) << 8) | s[3] as u32;
                dst.push(RtVertex { pos: v.world, uv: v.uv, color, mat: mi, pad: 0.0 });
            }
        }
        if opaque.is_empty() && alpha.is_empty() {
            return Ok(false);
        }
        let opaque_tris = (opaque.len() / 3) as u32;
        let alpha_tris = (alpha.len() / 3) as u32;
        let mut all = opaque;
        all.extend_from_slice(&alpha);

        let lights: Vec<RtLightGpu> = frame
            .lights()
            .iter()
            .take(256)
            .map(|l| RtLightGpu {
                pos_radius: [l.pos[0], l.pos[1], l.pos[2], l.radius],
                color_intensity: [l.color[0], l.color[1], l.color[2], l.intensity],
                direction_outer: [l.direction[0],l.direction[1],l.direction[2],l.cos_outer],
                inner_pad: [l.cos_inner,0.0,0.0,0.0],
            })
            .collect();

        self.ensure_images(be, w, h)?;

        // ---- upload ----------------------------------------------------------
        let mut fr = std::mem::take(&mut self.frames[fi]);
        let res = self.record_inner(be, &d, cmd, frame, &mut fr, &all, &mats, &lights, opaque_tris, alpha_tris, fog);
        self.frames[fi] = fr;
        res.map(|_| true)
    }

    #[allow(clippy::too_many_arguments)]
    unsafe fn record_inner(
        &mut self,
        be: &mut VkBackend,
        d: &ash::Device,
        cmd: vk::CommandBuffer,
        frame: &D64GfxFrame,
        fr: &mut RtFrame,
        all: &[RtVertex],
        mats: &[RtMaterial],
        lights: &[RtLightGpu],
        opaque_tris: u32,
        alpha_tris: u32,
        fog: [f32; 4],
    ) -> Result<(), String> {
        let geo_usage = vk::BufferUsageFlags::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR
            | vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS
            | vk::BufferUsageFlags::STORAGE_BUFFER;
        Self::ensure_buffer(be, &mut fr.verts, std::mem::size_of_val(all), geo_usage, true)?;
        fr.verts.write(0, as_bytes(all));
        Self::ensure_buffer(be, &mut fr.mats, std::mem::size_of_val(mats), vk::BufferUsageFlags::STORAGE_BUFFER, true)?;
        fr.mats.write(0, as_bytes(mats));
        let light_bytes = std::mem::size_of_val(lights).max(std::mem::size_of::<RtLightGpu>());
        Self::ensure_buffer(be, &mut fr.lights, light_bytes, vk::BufferUsageFlags::STORAGE_BUFFER, true)?;
        if !lights.is_empty() {
            fr.lights.write(0, as_bytes(lights));
        }

        // ---- uniforms -------------------------------------------------------
        let im = self.images.as_ref().unwrap();
        let viewproj = mat_mul(&frame.proj, &frame.view);
        let p = &be.rt_params;
        self.counter = self.counter.wrapping_add(1);
        let uni = RtUniforms {
            view: frame.view,
            proj: frame.proj,
            inv_view: mat_inv(&frame.view),
            inv_proj: mat_inv(&frame.proj),
            prev_viewproj: if self.history_valid { self.prev_viewproj } else { viewproj },
            cam_pos: [frame.cam_pos[0], frame.cam_pos[1], frame.cam_pos[2], 1.0],
            fog_color: fog,
            dims: [im.w, im.h, self.counter, lights.len() as u32],
            counts: [opaque_tris, alpha_tris, (self.history_valid && p.denoise) as u32, p.spp.max(1) as u32],
            params: [0.55, 0.5 * p.bounces.min(2) as f32, p.light_scale, 96.0],
            params2: [if p.denoise { 0.15 } else { 1.0 }, p.denoise as u32 as f32, p.sun as u32 as f32, p.bounces as f32],
            prev_cam: [self.prev_cam[0], self.prev_cam[1], self.prev_cam[2], 1.0],
        };
        Self::ensure_buffer(be, &mut fr.ubo, std::mem::size_of::<RtUniforms>(), vk::BufferUsageFlags::UNIFORM_BUFFER, true)?;
        fr.ubo.write(0, as_bytes(std::slice::from_ref(&uni)));
        self.prev_viewproj = viewproj;
        self.prev_cam = frame.cam_pos;

        // ---- BLAS -------------------------------------------------------------
        let stride = std::mem::size_of::<RtVertex>() as u64;
        let mk_geom = |addr: u64, verts: u32, opaque: bool| {
            vk::AccelerationStructureGeometryKHR::default()
                .geometry_type(vk::GeometryTypeKHR::TRIANGLES)
                .geometry(vk::AccelerationStructureGeometryDataKHR {
                    triangles: vk::AccelerationStructureGeometryTrianglesDataKHR::default()
                        .vertex_format(vk::Format::R32G32B32_SFLOAT)
                        .vertex_data(vk::DeviceOrHostAddressConstKHR { device_address: addr })
                        .vertex_stride(stride)
                        .max_vertex(verts.max(1) - 1)
                        .index_type(vk::IndexType::NONE_KHR),
                })
                .flags(if opaque { vk::GeometryFlagsKHR::OPAQUE } else { vk::GeometryFlagsKHR::empty() })
        };
        let geoms = [
            mk_geom(fr.verts.address, opaque_tris * 3, true),
            mk_geom(fr.verts.address + opaque_tris as u64 * 3 * stride, alpha_tris * 3, false),
        ];
        let counts = [opaque_tris, alpha_tris];
        let mut build = vk::AccelerationStructureBuildGeometryInfoKHR::default()
            .ty(vk::AccelerationStructureTypeKHR::BOTTOM_LEVEL)
            .flags(vk::BuildAccelerationStructureFlagsKHR::PREFER_FAST_BUILD)
            .mode(vk::BuildAccelerationStructureModeKHR::BUILD)
            .geometries(&geoms);
        let mut sizes = vk::AccelerationStructureBuildSizesInfoKHR::default();
        self.as_fn.get_acceleration_structure_build_sizes(
            vk::AccelerationStructureBuildTypeKHR::DEVICE,
            &build,
            &counts,
            &mut sizes,
        );
        let mut blas = std::mem::take(&mut fr.blas);
        self.ensure_accel(be, &mut blas, sizes.acceleration_structure_size, vk::AccelerationStructureTypeKHR::BOTTOM_LEVEL)?;
        fr.blas = blas;

        // TLAS sizes (one instance)
        let inst = vk::AccelerationStructureInstanceKHR {
            transform: vk::TransformMatrixKHR { matrix: [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0] },
            instance_custom_index_and_mask: vk::Packed24_8::new(0, 0xff),
            instance_shader_binding_table_record_offset_and_flags: vk::Packed24_8::new(
                0,
                vk::GeometryInstanceFlagsKHR::TRIANGLE_FACING_CULL_DISABLE.as_raw() as u8,
            ),
            acceleration_structure_reference: vk::AccelerationStructureReferenceKHR { device_handle: fr.blas.address },
        };
        Self::ensure_buffer(
            be,
            &mut fr.inst,
            std::mem::size_of::<vk::AccelerationStructureInstanceKHR>(),
            vk::BufferUsageFlags::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR | vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS,
            true,
        )?;
        fr.inst.write(
            0,
            std::slice::from_raw_parts(
                &inst as *const _ as *const u8,
                std::mem::size_of::<vk::AccelerationStructureInstanceKHR>(),
            ),
        );
        let tgeom = [vk::AccelerationStructureGeometryKHR::default()
            .geometry_type(vk::GeometryTypeKHR::INSTANCES)
            .geometry(vk::AccelerationStructureGeometryDataKHR {
                instances: vk::AccelerationStructureGeometryInstancesDataKHR::default()
                    .array_of_pointers(false)
                    .data(vk::DeviceOrHostAddressConstKHR { device_address: fr.inst.address }),
            })];
        let mut tbuild = vk::AccelerationStructureBuildGeometryInfoKHR::default()
            .ty(vk::AccelerationStructureTypeKHR::TOP_LEVEL)
            .flags(vk::BuildAccelerationStructureFlagsKHR::PREFER_FAST_TRACE)
            .mode(vk::BuildAccelerationStructureModeKHR::BUILD)
            .geometries(&tgeom);
        let mut tsizes = vk::AccelerationStructureBuildSizesInfoKHR::default();
        self.as_fn.get_acceleration_structure_build_sizes(
            vk::AccelerationStructureBuildTypeKHR::DEVICE,
            &tbuild,
            &[1],
            &mut tsizes,
        );
        let mut tlas = std::mem::take(&mut fr.tlas);
        self.ensure_accel(be, &mut tlas, tsizes.acceleration_structure_size, vk::AccelerationStructureTypeKHR::TOP_LEVEL)?;
        fr.tlas = tlas;

        // scratch: BLAS and TLAS regions, both aligned
        let a = self.scratch_align;
        let blas_scratch = sizes.build_scratch_size.div_ceil(a) * a;
        let scratch_total = blas_scratch + tsizes.build_scratch_size + 2 * a;
        Self::ensure_buffer(
            be,
            &mut fr.scratch,
            scratch_total as usize,
            vk::BufferUsageFlags::STORAGE_BUFFER | vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS,
            false,
        )?;
        let base = fr.scratch.address.div_ceil(a) * a;

        build = build
            .dst_acceleration_structure(fr.blas.handle)
            .scratch_data(vk::DeviceOrHostAddressKHR { device_address: base });
        let ranges = [
            vk::AccelerationStructureBuildRangeInfoKHR { primitive_count: opaque_tris, ..Default::default() },
            vk::AccelerationStructureBuildRangeInfoKHR { primitive_count: alpha_tris, ..Default::default() },
        ];
        self.as_fn.cmd_build_acceleration_structures(cmd, &[build], &[&ranges[..]]);
        let mb = vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::ACCELERATION_STRUCTURE_WRITE_KHR)
            .dst_access_mask(vk::AccessFlags::ACCELERATION_STRUCTURE_READ_KHR | vk::AccessFlags::ACCELERATION_STRUCTURE_WRITE_KHR);
        d.cmd_pipeline_barrier(
            cmd,
            vk::PipelineStageFlags::ACCELERATION_STRUCTURE_BUILD_KHR,
            vk::PipelineStageFlags::ACCELERATION_STRUCTURE_BUILD_KHR,
            vk::DependencyFlags::empty(),
            &[mb],
            &[],
            &[],
        );
        tbuild = tbuild
            .dst_acceleration_structure(fr.tlas.handle)
            .scratch_data(vk::DeviceOrHostAddressKHR { device_address: base + blas_scratch + a });
        let tranges = [vk::AccelerationStructureBuildRangeInfoKHR { primitive_count: 1, ..Default::default() }];
        self.as_fn.cmd_build_acceleration_structures(cmd, &[tbuild], &[&tranges[..]]);
        let mb2 = vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::ACCELERATION_STRUCTURE_WRITE_KHR)
            .dst_access_mask(vk::AccessFlags::ACCELERATION_STRUCTURE_READ_KHR | vk::AccessFlags::SHADER_READ);
        d.cmd_pipeline_barrier(
            cmd,
            vk::PipelineStageFlags::ACCELERATION_STRUCTURE_BUILD_KHR,
            vk::PipelineStageFlags::COMPUTE_SHADER,
            vk::DependencyFlags::empty(),
            &[mb2],
            &[],
            &[],
        );

        // ---- images to GENERAL on first use -----------------------------------
        let im = self.images.as_mut().unwrap();
        if im.fresh {
            for img in [&im.hist[0], &im.hist[1], &im.albedo, &im.nd, &im.depth, &im.color, &im.extra] {
                barrier(d, cmd, img.image, vk::ImageAspectFlags::COLOR, vk::ImageLayout::UNDEFINED, vk::ImageLayout::GENERAL);
            }
            im.fresh = false;
        }
        let (hist_in, hist_out) = (&im.hist[self.parity ^ 1], &im.hist[self.parity]);

        // ---- descriptors -------------------------------------------------------
        let white_view = be.textures[be.white as usize].as_ref().unwrap().image.view;
        let mut tex_infos: Vec<(u32, vk::DescriptorImageInfo)> = Vec::new();
        for id in 0..self.max_textures as usize {
            let view = match be.textures.get(id) {
                Some(Some(t)) => t.image.view,
                _ => white_view,
            };
            if fr.tex_mirror[id] != view {
                fr.tex_mirror[id] = view;
                tex_infos.push((
                    id as u32,
                    vk::DescriptorImageInfo {
                        sampler: vk::Sampler::null(),
                        image_view: view,
                        image_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
                    },
                ));
            }
        }
        let tlas_handles = [fr.tlas.handle];
        let mut as_write = vk::WriteDescriptorSetAccelerationStructureKHR::default().acceleration_structures(&tlas_handles);
        let buf_info = |b: &Buffer| [vk::DescriptorBufferInfo { buffer: b.buffer, offset: 0, range: vk::WHOLE_SIZE }];
        let bi_v = buf_info(&fr.verts);
        let bi_m = buf_info(&fr.mats);
        let bi_l = buf_info(&fr.lights);
        let bi_u = buf_info(&fr.ubo);
        let img_info = |i: &Image| [vk::DescriptorImageInfo { sampler: vk::Sampler::null(), image_view: i.view, image_layout: vk::ImageLayout::GENERAL }];
        let ii_hin = img_info(hist_in);
        let ii_hout = img_info(hist_out);
        let ii_alb = img_info(&im.albedo);
        let ii_nd = img_info(&im.nd);
        let ii_dep = img_info(&im.depth);
        let ii_col = img_info(&im.color);
        let ii_ext = img_info(&im.extra);
        let comp_info = |i: &Image| [vk::DescriptorImageInfo { sampler: self.sampler, image_view: i.view, image_layout: vk::ImageLayout::GENERAL }];
        let ci_col = comp_info(&im.color);
        let ci_dep = comp_info(&im.depth);
        let sb = vk::DescriptorType::STORAGE_BUFFER;
        let si = vk::DescriptorType::STORAGE_IMAGE;
        let w = |set: vk::DescriptorSet, binding: u32, ty: vk::DescriptorType| {
            vk::WriteDescriptorSet::default().dst_set(set).dst_binding(binding).descriptor_type(ty)
        };
        let mut writes = vec![
            w(fr.set_rt, 0, vk::DescriptorType::ACCELERATION_STRUCTURE_KHR).push_next(&mut as_write),
            w(fr.set_rt, 1, sb).buffer_info(&bi_v),
            w(fr.set_rt, 2, sb).buffer_info(&bi_m),
            w(fr.set_rt, 3, sb).buffer_info(&bi_l),
            w(fr.set_rt, 4, vk::DescriptorType::UNIFORM_BUFFER).buffer_info(&bi_u),
            w(fr.set_rt, 7, si).image_info(&ii_hin),
            w(fr.set_rt, 8, si).image_info(&ii_hout),
            w(fr.set_rt, 9, si).image_info(&ii_alb),
            w(fr.set_rt, 10, si).image_info(&ii_nd),
            w(fr.set_rt, 11, si).image_info(&ii_dep),
            w(fr.set_rt, 12, si).image_info(&ii_ext),
            w(fr.set_dn, 0, si).image_info(&ii_hout),
            w(fr.set_dn, 1, si).image_info(&ii_alb),
            w(fr.set_dn, 2, si).image_info(&ii_nd),
            w(fr.set_dn, 3, si).image_info(&ii_col),
            w(fr.set_dn, 4, vk::DescriptorType::UNIFORM_BUFFER).buffer_info(&bi_u),
            w(fr.set_dn, 5, si).image_info(&ii_ext),
            w(fr.set_comp, 0, vk::DescriptorType::COMBINED_IMAGE_SAMPLER).image_info(&ci_col),
            w(fr.set_comp, 1, vk::DescriptorType::COMBINED_IMAGE_SAMPLER).image_info(&ci_dep),
        ];
        writes[0].descriptor_count = 1;
        let tex_single: Vec<[vk::DescriptorImageInfo; 1]> = tex_infos.iter().map(|(_, i)| [*i]).collect();
        for (k, (id, _)) in tex_infos.iter().enumerate() {
            writes.push(
                w(fr.set_rt, 5, vk::DescriptorType::SAMPLED_IMAGE)
                    .dst_array_element(*id)
                    .image_info(&tex_single[k]),
            );
        }
        d.update_descriptor_sets(&writes, &[]);

        // ---- dispatch -----------------------------------------------------------
        let gx = im.w.div_ceil(8);
        let gy = im.h.div_ceil(8);
        d.cmd_bind_pipeline(cmd, vk::PipelineBindPoint::COMPUTE, self.pipe_rt);
        d.cmd_bind_descriptor_sets(cmd, vk::PipelineBindPoint::COMPUTE, self.pl_rt, 0, &[fr.set_rt], &[]);
        d.cmd_dispatch(cmd, gx, gy, 1);
        let mb3 = vk::MemoryBarrier::default()
            .src_access_mask(vk::AccessFlags::SHADER_WRITE)
            .dst_access_mask(vk::AccessFlags::SHADER_READ);
        d.cmd_pipeline_barrier(cmd, vk::PipelineStageFlags::COMPUTE_SHADER, vk::PipelineStageFlags::COMPUTE_SHADER,
                               vk::DependencyFlags::empty(), &[mb3], &[], &[]);
        d.cmd_bind_pipeline(cmd, vk::PipelineBindPoint::COMPUTE, self.pipe_dn);
        d.cmd_bind_descriptor_sets(cmd, vk::PipelineBindPoint::COMPUTE, self.pl_dn, 0, &[fr.set_dn], &[]);
        d.cmd_dispatch(cmd, gx, gy, 1);
        d.cmd_pipeline_barrier(cmd, vk::PipelineStageFlags::COMPUTE_SHADER, vk::PipelineStageFlags::FRAGMENT_SHADER,
                               vk::DependencyFlags::empty(), &[mb3], &[], &[]);

        self.parity ^= 1;
        self.history_valid = true;
        Ok(())
    }

    /// Draws the ray traced world into the current render pass.
    pub unsafe fn composite(&self, d: &ash::Device, cmd: vk::CommandBuffer, vp: vk::Viewport) {
        let fr = &self.frames[self.cur_slot];
        d.cmd_bind_pipeline(cmd, vk::PipelineBindPoint::GRAPHICS, self.pipe_comp);
        d.cmd_set_viewport(cmd, 0, &[vp]);
        let x = vp.x as i32;
        let y = (vp.y + vp.height) as i32;
        let sc = vk::Rect2D {
            offset: vk::Offset2D { x: x.max(0), y: y.max(0) },
            extent: vk::Extent2D { width: vp.width as u32, height: (-vp.height) as u32 },
        };
        d.cmd_set_scissor(cmd, 0, &[sc]);
        d.cmd_bind_descriptor_sets(cmd, vk::PipelineBindPoint::GRAPHICS, self.pl_comp, 0, &[fr.set_comp], &[]);
        d.cmd_draw(cmd, 3, 1, 0, 0);
    }

    /// Forget accumulated history (e.g. after a level change or camera cut).
    pub fn reset_history(&mut self) {
        self.history_valid = false;
    }

    pub unsafe fn destroy(&mut self, be: &mut VkBackend) {
        let d = be.core.device.clone();
        for mut fr in self.frames.drain(..) {
            for b in [&mut fr.verts, &mut fr.mats, &mut fr.lights, &mut fr.ubo, &mut fr.inst, &mut fr.scratch] {
                b.destroy(&d, &mut be.core.alloc);
            }
            for acc in [&mut fr.blas, &mut fr.tlas] {
                if acc.handle != vk::AccelerationStructureKHR::null() {
                    self.as_fn.destroy_acceleration_structure(acc.handle, None);
                    acc.buffer.destroy(&d, &mut be.core.alloc);
                }
            }
        }
        if let Some(mut im) = self.images.take() {
            for img in im.hist.iter_mut().chain([&mut im.albedo, &mut im.nd, &mut im.depth, &mut im.color, &mut im.extra]) {
                img.destroy(&d, &mut be.core.alloc);
            }
        }
        d.destroy_sampler(self.sampler, None);
        d.destroy_descriptor_pool(self.pool, None);
        for p in [self.pipe_rt, self.pipe_dn, self.pipe_comp] {
            d.destroy_pipeline(p, None);
        }
        for l in [self.pl_rt, self.pl_dn, self.pl_comp] {
            d.destroy_pipeline_layout(l, None);
        }
        for l in [self.dsl_rt, self.dsl_dn, self.dsl_comp] {
            d.destroy_descriptor_set_layout(l, None);
        }
    }
}
