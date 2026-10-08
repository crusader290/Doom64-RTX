//! Vulkan 1.1+ backend (ash).
//!
//! Frame flow:
//!   1. texture uploads queued since the last frame are recorded first;
//!   2. if ray tracing is on and the frame has a 3D view, the world geometry
//!      is ray traced (rt.rs) into an image sized like the game viewport;
//!   3. one render pass draws the interpreted N64 commands into an offscreen
//!      scene image; at the first world command the ray traced image is
//!      composited, and the world commands it replaced are skipped;
//!   4. the scene image is blitted to the swapchain.
//! The offscreen scene also serves `read_frame` (screen wipes, screenshots).

mod mem;
mod rt;

use crate::ffi::*;
use crate::gl_backend::{pack_rgba, sampler_index};
use crate::shaders;
use crate::{game_viewport, rescale_rgba, Backend, RtParams};
use ash::{khr, vk};
use mem::{barrier, Allocator, Buffer, Image};
use std::collections::HashMap;
use std::ffi::{c_char, CStr, CString};

const FRAMES: usize = 2;
const SCENE_FORMAT: vk::Format = vk::Format::R8G8B8A8_UNORM;
const DEPTH_FORMAT: vk::Format = vk::Format::D32_SFLOAT;

struct Texture {
    image: Image,
    sets: Vec<vk::DescriptorSet>,
}

struct PendingUpload {
    id: u32,
    w: u32,
    h: u32,
    data: Vec<u8>,
}

#[derive(Default)]
struct Frame {
    cmd: vk::CommandBuffer,
    fence: vk::Fence,
    image_available: vk::Semaphore,
    vbuf: Buffer,
    staging: Buffer,
    dead_images: Vec<Image>,
    dead_sets: Vec<vk::DescriptorSet>,
    dead_buffers: Vec<Buffer>,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct PushConstants {
    cc: [u32; 4],
    colors: [u32; 4],
    flags: u32,
    plod: f32,
}

pub(crate) struct Core {
    pub entry: ash::Entry,
    pub instance: ash::Instance,
    pub pdev: vk::PhysicalDevice,
    pub device: ash::Device,
    pub queue: vk::Queue,
    pub qfamily: u32,
    pub alloc: Allocator,
    pub api_version: u32,
}

pub struct VkBackend {
    window: Window,
    core: Core,
    surface_fn: khr::surface::Instance,
    surface: vk::SurfaceKHR,
    swapchain_fn: khr::swapchain::Device,
    swapchain: vk::SwapchainKHR,
    sc_images: Vec<vk::Image>,
    sc_format: vk::Format,
    sc_extent: vk::Extent2D,
    sc_render_done: Vec<vk::Semaphore>,
    cmd_pool: vk::CommandPool,
    frames: Vec<Frame>,
    frame_idx: usize,
    scene: Image,
    depth: Image,
    scene_layout: vk::ImageLayout,
    render_pass: vk::RenderPass,
    framebuffer: vk::Framebuffer,
    dsl_tex: vk::DescriptorSetLayout,
    pipeline_layout: vk::PipelineLayout,
    pipelines: [vk::Pipeline; 4], // [blend][depth_test]
    samplers: Vec<vk::Sampler>,
    desc_pools: Vec<vk::DescriptorPool>,
    desc_cache: HashMap<(u32, u8), (vk::DescriptorSet, usize)>,
    textures: Vec<Option<Texture>>,
    free_ids: Vec<u32>,
    pending: Vec<PendingUpload>,
    white: u32,
    vsync: bool,
    device_name: String,
    game_rect: (i32, i32, u32, u32),
    readback: Buffer,
    rt: Option<rt::RayTracer>,
    rt_supported: bool,
    rt_enabled: bool,
    rt_params: RtParams,
    swapchain_dirty: bool,
    virtual_width: f32,
}

fn vkerr(what: &str) -> impl Fn(vk::Result) -> String + '_ {
    move |e| format!("{what}: {e}")
}

unsafe fn create_shader(device: &ash::Device, bytes: &[u8]) -> Result<vk::ShaderModule, String> {
    let words = shaders::spv(bytes);
    device
        .create_shader_module(&vk::ShaderModuleCreateInfo::default().code(&words), None)
        .map_err(vkerr("vkCreateShaderModule"))
}

impl VkBackend {
    pub fn new(info: &D64GfxInitInfo, window: Window) -> Result<Self, String> {
        unsafe { Self::new_inner(info, window) }
    }

    unsafe fn new_inner(info: &D64GfxInitInfo, window: Window) -> Result<Self, String> {
        let w = &window.0;
        let gipa = w.vk_get_instance_proc_addr.ok_or("no Vulkan loader callback")?(w.user);
        if gipa.is_null() {
            return Err("Vulkan loader not available (SDL_Vulkan_GetVkGetInstanceProcAddr failed)".into());
        }
        let static_fn = ash::StaticFn { get_instance_proc_addr: std::mem::transmute::<*mut std::ffi::c_void, vk::PFN_vkGetInstanceProcAddr>(gipa) };
        let entry = ash::Entry::from_static_fn(static_fn);

        // --- instance -------------------------------------------------
        let loader_version = entry.try_enumerate_instance_version().ok().flatten().unwrap_or(vk::API_VERSION_1_0);
        if loader_version < vk::API_VERSION_1_1 {
            return Err("Vulkan 1.1 is required".into());
        }
        let api_version = loader_version.min(vk::API_VERSION_1_3);
        let mut ext_count = 0u32;
        let exts_ptr = w.vk_instance_extensions.ok_or("no Vulkan extension callback")?(w.user, &mut ext_count);
        let mut exts: Vec<*const c_char> = if exts_ptr.is_null() {
            Vec::new()
        } else {
            std::slice::from_raw_parts(exts_ptr, ext_count as usize).to_vec()
        };
        let available_exts = entry.enumerate_instance_extension_properties(None).unwrap_or_default();
        let has_ext = |name: &CStr| available_exts.iter().any(|e| e.extension_name_as_c_str() == Ok(name));
        let mut layers: Vec<*const c_char> = Vec::new();
        let validation = CString::new("VK_LAYER_KHRONOS_validation").unwrap();
        if info.validation != 0 {
            let avail = entry.enumerate_instance_layer_properties().unwrap_or_default();
            if avail.iter().any(|l| l.layer_name_as_c_str() == Ok(validation.as_c_str())) {
                layers.push(validation.as_ptr());
                if has_ext(ash::ext::debug_utils::NAME) {
                    exts.push(ash::ext::debug_utils::NAME.as_ptr());
                }
            } else {
                window.log(1, "Vulkan validation layer requested but not installed");
            }
        }
        let app_name = CString::new("Doom64-RTX").unwrap();
        let app = vk::ApplicationInfo::default()
            .application_name(&app_name)
            .engine_name(&app_name)
            .api_version(api_version);
        let ici = vk::InstanceCreateInfo::default()
            .application_info(&app)
            .enabled_extension_names(&exts)
            .enabled_layer_names(&layers);
        let instance = entry.create_instance(&ici, None).map_err(vkerr("vkCreateInstance"))?;

        // --- surface ----------------------------------------------------
        let surface_fn = khr::surface::Instance::new(&entry, &instance);
        let mut surface_raw = 0u64;
        let create_surface = w.vk_create_surface.ok_or("no surface callback")?;
        if create_surface(w.user, vk::Handle::as_raw(instance.handle()), &mut surface_raw) == 0 {
            instance.destroy_instance(None);
            return Err("could not create a Vulkan surface for the window".into());
        }
        let surface = <vk::SurfaceKHR as vk::Handle>::from_raw(surface_raw);

        // --- physical device -------------------------------------------
        let pdevs = instance.enumerate_physical_devices().map_err(vkerr("enumerate devices"))?;
        let mut candidates = Vec::new();
        for (i, &pd) in pdevs.iter().enumerate() {
            let props = instance.get_physical_device_properties(pd);
            if props.api_version < vk::API_VERSION_1_1 {
                continue;
            }
            let qfams = instance.get_physical_device_queue_family_properties(pd);
            let q = qfams.iter().enumerate().position(|(qi, q)| {
                q.queue_flags.contains(vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE)
                    && surface_fn.get_physical_device_surface_support(pd, qi as u32, surface).unwrap_or(false)
            });
            let Some(q) = q else { continue };
            let score = match props.device_type {
                vk::PhysicalDeviceType::DISCRETE_GPU => 3,
                vk::PhysicalDeviceType::INTEGRATED_GPU => 2,
                vk::PhysicalDeviceType::VIRTUAL_GPU => 1,
                _ => 0,
            };
            let score = if info.gpu_index >= 0 && info.gpu_index as usize == i { 100 } else { score };
            candidates.push((score, pd, q as u32, props));
        }
        candidates.sort_by(|a, b| b.0.cmp(&a.0));
        let Some(&(_, pdev, qfamily, props)) = candidates.first() else {
            surface_fn.destroy_surface(surface, None);
            instance.destroy_instance(None);
            return Err("no Vulkan 1.1 device can present to this window".into());
        };
        let device_name = props.device_name_as_c_str().map(|c| c.to_string_lossy().into_owned()).unwrap_or_default();
        let dev_api = props.api_version.min(api_version);

        // --- device (+ optional ray tracing) ---------------------------
        let dev_exts = instance.enumerate_device_extension_properties(pdev).unwrap_or_default();
        let dev_has = |name: &CStr| dev_exts.iter().any(|e| e.extension_name_as_c_str() == Ok(name));
        let mut enable_exts: Vec<*const c_char> = vec![khr::swapchain::NAME.as_ptr()];
        let rt_exts_ok = dev_api >= vk::API_VERSION_1_2
            && dev_has(khr::acceleration_structure::NAME)
            && dev_has(khr::ray_query::NAME)
            && dev_has(khr::deferred_host_operations::NAME);
        let mut rt_supported = false;
        let mut f12 = vk::PhysicalDeviceVulkan12Features::default();
        let mut f_as = vk::PhysicalDeviceAccelerationStructureFeaturesKHR::default();
        let mut f_rq = vk::PhysicalDeviceRayQueryFeaturesKHR::default();
        if rt_exts_ok {
            let mut q12 = vk::PhysicalDeviceVulkan12Features::default();
            let mut qas = vk::PhysicalDeviceAccelerationStructureFeaturesKHR::default();
            let mut qrq = vk::PhysicalDeviceRayQueryFeaturesKHR::default();
            let mut f2 = vk::PhysicalDeviceFeatures2::default().push_next(&mut q12).push_next(&mut qas).push_next(&mut qrq);
            instance.get_physical_device_features2(pdev, &mut f2);
            if q12.buffer_device_address != 0 && qas.acceleration_structure != 0 && qrq.ray_query != 0 {
                rt_supported = true;
                f12.buffer_device_address = 1;
                f12.descriptor_indexing = q12.descriptor_indexing;
                f12.runtime_descriptor_array = q12.runtime_descriptor_array;
                f12.shader_sampled_image_array_non_uniform_indexing = q12.shader_sampled_image_array_non_uniform_indexing;
                f12.descriptor_binding_partially_bound = q12.descriptor_binding_partially_bound;
                f12.descriptor_binding_variable_descriptor_count = q12.descriptor_binding_variable_descriptor_count;
                f12.scalar_block_layout = q12.scalar_block_layout;
                f_as.acceleration_structure = 1;
                f_rq.ray_query = 1;
                enable_exts.push(khr::acceleration_structure::NAME.as_ptr());
                enable_exts.push(khr::ray_query::NAME.as_ptr());
                enable_exts.push(khr::deferred_host_operations::NAME.as_ptr());
                if q12.runtime_descriptor_array == 0
                    || q12.shader_sampled_image_array_non_uniform_indexing == 0
                    || q12.descriptor_binding_partially_bound == 0
                {
                    rt_supported = false;
                }
            }
        }
        if !rt_supported {
            // keep the RT extensions out of the list
            enable_exts.truncate(1);
        }
        let prio = [1.0f32];
        let qci = [vk::DeviceQueueCreateInfo::default().queue_family_index(qfamily).queue_priorities(&prio)];
        let mut features2 = vk::PhysicalDeviceFeatures2::default();
        let mut dci = vk::DeviceCreateInfo::default().queue_create_infos(&qci).enabled_extension_names(&enable_exts);
        if rt_supported {
            features2 = features2.push_next(&mut f12).push_next(&mut f_as).push_next(&mut f_rq);
            dci = dci.push_next(&mut features2);
        }
        let device = instance.create_device(pdev, &dci, None).map_err(vkerr("vkCreateDevice"))?;
        let queue = device.get_device_queue(qfamily, 0);
        let mem_props = instance.get_physical_device_memory_properties(pdev);
        let core = Core {
            entry,
            instance,
            pdev,
            device,
            queue,
            qfamily,
            alloc: Allocator::new(mem_props, rt_supported),
            api_version: dev_api,
        };
        let swapchain_fn = khr::swapchain::Device::new(&core.instance, &core.device);

        let d = &core.device;
        let cmd_pool = d
            .create_command_pool(
                &vk::CommandPoolCreateInfo::default()
                    .queue_family_index(qfamily)
                    .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER),
                None,
            )
            .map_err(vkerr("command pool"))?;
        let cmds = d
            .allocate_command_buffers(
                &vk::CommandBufferAllocateInfo::default()
                    .command_pool(cmd_pool)
                    .level(vk::CommandBufferLevel::PRIMARY)
                    .command_buffer_count(FRAMES as u32),
            )
            .map_err(vkerr("command buffers"))?;
        let mut frames = Vec::new();
        for &cmd in cmds.iter() {
            frames.push(Frame {
                cmd,
                fence: d
                    .create_fence(&vk::FenceCreateInfo::default().flags(vk::FenceCreateFlags::SIGNALED), None)
                    .map_err(vkerr("fence"))?,
                image_available: d.create_semaphore(&vk::SemaphoreCreateInfo::default(), None).map_err(vkerr("semaphore"))?,
                ..Default::default()
            });
        }

        // --- static pipeline objects -----------------------------------
        let render_pass = Self::create_render_pass(d)?;
        let bindings = [vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
            .descriptor_count(1)
            .stage_flags(vk::ShaderStageFlags::FRAGMENT)];
        let dsl_tex = d
            .create_descriptor_set_layout(&vk::DescriptorSetLayoutCreateInfo::default().bindings(&bindings), None)
            .map_err(vkerr("set layout"))?;
        let pcr = [vk::PushConstantRange::default()
            .stage_flags(vk::ShaderStageFlags::FRAGMENT)
            .offset(0)
            .size(std::mem::size_of::<PushConstants>() as u32)];
        let set_layouts = [dsl_tex, dsl_tex];
        let pipeline_layout = d
            .create_pipeline_layout(
                &vk::PipelineLayoutCreateInfo::default().set_layouts(&set_layouts).push_constant_ranges(&pcr),
                None,
            )
            .map_err(vkerr("pipeline layout"))?;
        let pipelines = Self::create_raster_pipelines(d, render_pass, pipeline_layout)?;

        let mut samplers = Vec::new();
        for filter in [false, true] {
            for s in 0..3u8 {
                for t in 0..3u8 {
                    let am = |m: u8| match m {
                        WRAP_MIRROR => vk::SamplerAddressMode::MIRRORED_REPEAT,
                        WRAP_CLAMP => vk::SamplerAddressMode::CLAMP_TO_EDGE,
                        _ => vk::SamplerAddressMode::REPEAT,
                    };
                    let f = if filter { vk::Filter::LINEAR } else { vk::Filter::NEAREST };
                    let si = vk::SamplerCreateInfo::default()
                        .mag_filter(f)
                        .min_filter(f)
                        .mipmap_mode(vk::SamplerMipmapMode::NEAREST)
                        .address_mode_u(am(s))
                        .address_mode_v(am(t))
                        .address_mode_w(vk::SamplerAddressMode::REPEAT)
                        .max_lod(0.0);
                    samplers.push(d.create_sampler(&si, None).map_err(vkerr("sampler"))?);
                }
            }
        }

        let mut me = VkBackend {
            window,
            core,
            surface_fn,
            surface,
            swapchain_fn,
            swapchain: vk::SwapchainKHR::null(),
            sc_images: Vec::new(),
            sc_format: vk::Format::UNDEFINED,
            sc_extent: vk::Extent2D::default(),
            sc_render_done: Vec::new(),
            cmd_pool,
            frames,
            frame_idx: 0,
            scene: Image::default(),
            depth: Image::default(),
            scene_layout: vk::ImageLayout::UNDEFINED,
            render_pass,
            framebuffer: vk::Framebuffer::null(),
            dsl_tex,
            pipeline_layout,
            pipelines,
            samplers,
            desc_pools: Vec::new(),
            desc_cache: HashMap::new(),
            textures: vec![None],
            free_ids: Vec::new(),
            pending: Vec::new(),
            white: 0,
            vsync: info.vsync != 0,
            device_name,
            game_rect: (0, 0, 1, 1),
            readback: Buffer::default(),
            rt: None,
            rt_supported,
            rt_enabled: false,
            rt_params: RtParams::default(),
            swapchain_dirty: false,
            virtual_width: 320.0,
        };
        me.create_swapchain()?;
        me.white = me.texture_create(1, 1, &[255, 255, 255, 255]);
        if rt_supported && info.raytracing != 0 {
            me.set_raytracing(true);
        }
        me.window.log(
            0,
            &format!(
                "Vulkan {}.{} on {} (ray tracing {})",
                vk::api_version_major(dev_api),
                vk::api_version_minor(dev_api),
                me.device_name,
                if rt_supported { "supported" } else { "not supported" }
            ),
        );
        Ok(me)
    }

    unsafe fn create_render_pass(d: &ash::Device) -> Result<vk::RenderPass, String> {
        let atts = [
            vk::AttachmentDescription::default()
                .format(SCENE_FORMAT)
                .samples(vk::SampleCountFlags::TYPE_1)
                .load_op(vk::AttachmentLoadOp::CLEAR)
                .store_op(vk::AttachmentStoreOp::STORE)
                .stencil_load_op(vk::AttachmentLoadOp::DONT_CARE)
                .stencil_store_op(vk::AttachmentStoreOp::DONT_CARE)
                .initial_layout(vk::ImageLayout::UNDEFINED)
                .final_layout(vk::ImageLayout::TRANSFER_SRC_OPTIMAL),
            vk::AttachmentDescription::default()
                .format(DEPTH_FORMAT)
                .samples(vk::SampleCountFlags::TYPE_1)
                .load_op(vk::AttachmentLoadOp::CLEAR)
                .store_op(vk::AttachmentStoreOp::DONT_CARE)
                .stencil_load_op(vk::AttachmentLoadOp::DONT_CARE)
                .stencil_store_op(vk::AttachmentStoreOp::DONT_CARE)
                .initial_layout(vk::ImageLayout::UNDEFINED)
                .final_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
        ];
        let color_ref = [vk::AttachmentReference { attachment: 0, layout: vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL }];
        let depth_ref = vk::AttachmentReference { attachment: 1, layout: vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        let subpass = [vk::SubpassDescription::default()
            .pipeline_bind_point(vk::PipelineBindPoint::GRAPHICS)
            .color_attachments(&color_ref)
            .depth_stencil_attachment(&depth_ref)];
        let deps = [vk::SubpassDependency::default()
            .src_subpass(vk::SUBPASS_EXTERNAL)
            .dst_subpass(0)
            .src_stage_mask(vk::PipelineStageFlags::ALL_COMMANDS)
            .dst_stage_mask(vk::PipelineStageFlags::ALL_GRAPHICS)
            .src_access_mask(vk::AccessFlags::MEMORY_WRITE)
            .dst_access_mask(vk::AccessFlags::MEMORY_READ | vk::AccessFlags::MEMORY_WRITE)];
        d.create_render_pass(
            &vk::RenderPassCreateInfo::default().attachments(&atts).subpasses(&subpass).dependencies(&deps),
            None,
        )
        .map_err(vkerr("render pass"))
    }

    unsafe fn create_raster_pipelines(
        d: &ash::Device,
        render_pass: vk::RenderPass,
        layout: vk::PipelineLayout,
    ) -> Result<[vk::Pipeline; 4], String> {
        let vs = create_shader(d, shaders::VK_RASTER_VERT)?;
        let fs = create_shader(d, shaders::VK_RASTER_FRAG)?;
        let main = CString::new("main").unwrap();
        let stages = [
            vk::PipelineShaderStageCreateInfo::default().stage(vk::ShaderStageFlags::VERTEX).module(vs).name(&main),
            vk::PipelineShaderStageCreateInfo::default().stage(vk::ShaderStageFlags::FRAGMENT).module(fs).name(&main),
        ];
        let stride = std::mem::size_of::<D64GfxVertex>() as u32;
        let vb = [vk::VertexInputBindingDescription { binding: 0, stride, input_rate: vk::VertexInputRate::VERTEX }];
        let va = [
            vk::VertexInputAttributeDescription { location: 0, binding: 0, format: vk::Format::R32G32B32A32_SFLOAT, offset: 0 },
            vk::VertexInputAttributeDescription { location: 1, binding: 0, format: vk::Format::R32G32_SFLOAT, offset: 16 },
            vk::VertexInputAttributeDescription { location: 2, binding: 0, format: vk::Format::R8G8B8A8_UNORM, offset: 24 },
            vk::VertexInputAttributeDescription { location: 3, binding: 0, format: vk::Format::R32G32_SFLOAT, offset: 40 },
        ];
        let vi = vk::PipelineVertexInputStateCreateInfo::default()
            .vertex_binding_descriptions(&vb)
            .vertex_attribute_descriptions(&va);
        let ia = vk::PipelineInputAssemblyStateCreateInfo::default().topology(vk::PrimitiveTopology::TRIANGLE_LIST);
        let vp = vk::PipelineViewportStateCreateInfo::default().viewport_count(1).scissor_count(1);
        let rs = vk::PipelineRasterizationStateCreateInfo::default()
            .polygon_mode(vk::PolygonMode::FILL)
            .cull_mode(vk::CullModeFlags::NONE)
            .front_face(vk::FrontFace::COUNTER_CLOCKWISE)
            .line_width(1.0);
        let ms = vk::PipelineMultisampleStateCreateInfo::default().rasterization_samples(vk::SampleCountFlags::TYPE_1);
        let dyn_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
        let dy = vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dyn_states);
        let mut out = [vk::Pipeline::null(); 4];
        for blend in 0..2 {
            for depth in 0..2 {
                let att = [vk::PipelineColorBlendAttachmentState::default()
                    .blend_enable(blend == 1)
                    .src_color_blend_factor(vk::BlendFactor::SRC_ALPHA)
                    .dst_color_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA)
                    .color_blend_op(vk::BlendOp::ADD)
                    .src_alpha_blend_factor(vk::BlendFactor::ONE)
                    .dst_alpha_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA)
                    .alpha_blend_op(vk::BlendOp::ADD)
                    .color_write_mask(vk::ColorComponentFlags::RGBA)];
                let cb = vk::PipelineColorBlendStateCreateInfo::default().attachments(&att);
                let ds = vk::PipelineDepthStencilStateCreateInfo::default()
                    .depth_test_enable(depth == 1)
                    .depth_write_enable(false)
                    .depth_compare_op(vk::CompareOp::LESS_OR_EQUAL);
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
                    .map_err(|(_, e)| format!("graphics pipeline: {e}"))?;
                out[blend * 2 + depth] = p[0];
            }
        }
        d.destroy_shader_module(vs, None);
        d.destroy_shader_module(fs, None);
        Ok(out)
    }

    unsafe fn destroy_swapchain_resources(&mut self) {
        let d = &self.core.device;
        if self.framebuffer != vk::Framebuffer::null() {
            d.destroy_framebuffer(self.framebuffer, None);
            self.framebuffer = vk::Framebuffer::null();
        }
        self.scene.destroy(d, &mut self.core.alloc);
        self.depth.destroy(d, &mut self.core.alloc);
        for s in self.sc_render_done.drain(..) {
            d.destroy_semaphore(s, None);
        }
        if self.swapchain != vk::SwapchainKHR::null() {
            self.swapchain_fn.destroy_swapchain(self.swapchain, None);
            self.swapchain = vk::SwapchainKHR::null();
        }
        self.sc_images.clear();
    }

    unsafe fn create_swapchain(&mut self) -> Result<(), String> {
        self.core.device.device_wait_idle().ok();
        self.destroy_swapchain_resources();
        let pd = self.core.pdev;
        let caps = self
            .surface_fn
            .get_physical_device_surface_capabilities(pd, self.surface)
            .map_err(vkerr("surface caps"))?;
        let formats = self
            .surface_fn
            .get_physical_device_surface_formats(pd, self.surface)
            .map_err(vkerr("surface formats"))?;
        let fmt = formats
            .iter()
            .find(|f| f.format == vk::Format::B8G8R8A8_UNORM || f.format == vk::Format::R8G8B8A8_UNORM)
            .copied()
            .unwrap_or(formats[0]);
        let modes = self
            .surface_fn
            .get_physical_device_surface_present_modes(pd, self.surface)
            .unwrap_or_default();
        let mode = if self.vsync {
            vk::PresentModeKHR::FIFO
        } else if modes.contains(&vk::PresentModeKHR::MAILBOX) {
            vk::PresentModeKHR::MAILBOX
        } else if modes.contains(&vk::PresentModeKHR::IMMEDIATE) {
            vk::PresentModeKHR::IMMEDIATE
        } else {
            vk::PresentModeKHR::FIFO
        };
        let (ww, wh) = self.window.drawable_size();
        let extent = if caps.current_extent.width != u32::MAX {
            caps.current_extent
        } else {
            vk::Extent2D {
                width: ww.clamp(caps.min_image_extent.width, caps.max_image_extent.width),
                height: wh.clamp(caps.min_image_extent.height, caps.max_image_extent.height),
            }
        };
        let extent = vk::Extent2D { width: extent.width.max(1), height: extent.height.max(1) };
        let mut count = caps.min_image_count + 1;
        if caps.max_image_count > 0 {
            count = count.min(caps.max_image_count);
        }
        let sci = vk::SwapchainCreateInfoKHR::default()
            .surface(self.surface)
            .min_image_count(count)
            .image_format(fmt.format)
            .image_color_space(fmt.color_space)
            .image_extent(extent)
            .image_array_layers(1)
            .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_DST)
            .image_sharing_mode(vk::SharingMode::EXCLUSIVE)
            .pre_transform(caps.current_transform)
            .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
            .present_mode(mode)
            .clipped(true);
        self.swapchain = self.swapchain_fn.create_swapchain(&sci, None).map_err(vkerr("swapchain"))?;
        self.sc_images = self.swapchain_fn.get_swapchain_images(self.swapchain).map_err(vkerr("swapchain images"))?;
        self.sc_format = fmt.format;
        self.sc_extent = extent;
        let d = &self.core.device;
        for _ in 0..self.sc_images.len() {
            self.sc_render_done
                .push(d.create_semaphore(&vk::SemaphoreCreateInfo::default(), None).map_err(vkerr("semaphore"))?);
        }
        self.scene = Image::new(
            d,
            &mut self.core.alloc,
            extent.width,
            extent.height,
            SCENE_FORMAT,
            vk::ImageUsageFlags::COLOR_ATTACHMENT | vk::ImageUsageFlags::TRANSFER_SRC | vk::ImageUsageFlags::SAMPLED,
            vk::ImageAspectFlags::COLOR,
        )?;
        self.depth = Image::new(
            d,
            &mut self.core.alloc,
            extent.width,
            extent.height,
            DEPTH_FORMAT,
            vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT,
            vk::ImageAspectFlags::DEPTH,
        )?;
        let views = [self.scene.view, self.depth.view];
        self.framebuffer = d
            .create_framebuffer(
                &vk::FramebufferCreateInfo::default()
                    .render_pass(self.render_pass)
                    .attachments(&views)
                    .width(extent.width)
                    .height(extent.height)
                    .layers(1),
                None,
            )
            .map_err(vkerr("framebuffer"))?;
        self.scene_layout = vk::ImageLayout::UNDEFINED;
        self.swapchain_dirty = false;
        self.game_rect = game_viewport(extent.width, extent.height, self.virtual_width);
        Ok(())
    }

    unsafe fn descriptor_for(&mut self, id: u32, sampler: u8) -> vk::DescriptorSet {
        let id = if self.textures.get(id as usize).map(|t| t.is_some()).unwrap_or(false) { id } else { self.white };
        if let Some(&(s, _)) = self.desc_cache.get(&(id, sampler)) {
            return s;
        }
        let d = &self.core.device;
        let mut set = None;
        for (pi, &pool) in self.desc_pools.iter().enumerate() {
            let layouts = [self.dsl_tex];
            if let Ok(s) = d.allocate_descriptor_sets(
                &vk::DescriptorSetAllocateInfo::default().descriptor_pool(pool).set_layouts(&layouts),
            ) {
                set = Some((s[0], pi));
                break;
            }
        }
        if set.is_none() {
            let sizes = [vk::DescriptorPoolSize { ty: vk::DescriptorType::COMBINED_IMAGE_SAMPLER, descriptor_count: 2048 }];
            let pool = d
                .create_descriptor_pool(
                    &vk::DescriptorPoolCreateInfo::default()
                        .flags(vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET)
                        .max_sets(2048)
                        .pool_sizes(&sizes),
                    None,
                )
                .expect("descriptor pool");
            self.desc_pools.push(pool);
            let layouts = [self.dsl_tex];
            let s = d
                .allocate_descriptor_sets(&vk::DescriptorSetAllocateInfo::default().descriptor_pool(pool).set_layouts(&layouts))
                .expect("descriptor set");
            set = Some((s[0], self.desc_pools.len() - 1));
        }
        let (set, pool_index) = set.unwrap();
        let tex = self.textures[id as usize].as_mut().unwrap();
        let ii = [vk::DescriptorImageInfo {
            sampler: self.samplers[sampler as usize],
            image_view: tex.image.view,
            image_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
        }];
        let write = vk::WriteDescriptorSet::default()
            .dst_set(set)
            .dst_binding(0)
            .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
            .image_info(&ii);
        d.update_descriptor_sets(&[write], &[]);
        tex.sets.push(set);
        self.desc_cache.insert((id, sampler), (set, pool_index));
        set
    }

    unsafe fn record_uploads(&mut self, fi: usize) -> Result<(), String> {
        if self.pending.is_empty() {
            return Ok(());
        }
        let total: usize = self.pending.iter().map(|p| p.data.len().next_multiple_of(16)).sum();
        let d = &self.core.device;
        if (self.frames[fi].staging.size as usize) < total {
            let mut old = std::mem::take(&mut self.frames[fi].staging);
            old.destroy(d, &mut self.core.alloc);
            self.frames[fi].staging = Buffer::new(
                d,
                &mut self.core.alloc,
                (total as u64).next_power_of_two().max(1 << 20),
                vk::BufferUsageFlags::TRANSFER_SRC,
                true,
            )?;
        }
        let cmd = self.frames[fi].cmd;
        let mut off = 0usize;
        let pending = std::mem::take(&mut self.pending);
        for p in pending {
            let Some(Some(tex)) = self.textures.get(p.id as usize) else { continue };
            self.frames[fi].staging.write(off, &p.data);
            barrier(d, cmd, tex.image.image, vk::ImageAspectFlags::COLOR, vk::ImageLayout::UNDEFINED,
                    vk::ImageLayout::TRANSFER_DST_OPTIMAL);
            let region = vk::BufferImageCopy::default()
                .buffer_offset(off as u64)
                .image_subresource(vk::ImageSubresourceLayers {
                    aspect_mask: vk::ImageAspectFlags::COLOR,
                    mip_level: 0,
                    base_array_layer: 0,
                    layer_count: 1,
                })
                .image_extent(vk::Extent3D { width: p.w, height: p.h, depth: 1 });
            d.cmd_copy_buffer_to_image(cmd, self.frames[fi].staging.buffer, tex.image.image,
                                       vk::ImageLayout::TRANSFER_DST_OPTIMAL, &[region]);
            barrier(d, cmd, tex.image.image, vk::ImageAspectFlags::COLOR, vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                    vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL);
            off += p.data.len().next_multiple_of(16);
        }
        Ok(())
    }

    unsafe fn render_inner(&mut self, frame: &D64GfxFrame) -> Result<(), String> {
        let (ww, wh) = self.window.drawable_size();
        if self.swapchain_dirty || ww != self.sc_extent.width || wh != self.sc_extent.height {
            self.create_swapchain()?;
        }
        if frame.vwidth() != self.virtual_width {
            self.virtual_width = frame.vwidth();
            self.game_rect = game_viewport(self.sc_extent.width, self.sc_extent.height, self.virtual_width);
        }
        let fi = self.frame_idx;
        let d = self.core.device.clone();
        d.wait_for_fences(&[self.frames[fi].fence], true, u64::MAX).map_err(vkerr("wait fence"))?;

        // release resources this frame slot was still using
        for mut img in std::mem::take(&mut self.frames[fi].dead_images) {
            img.destroy(&d, &mut self.core.alloc);
        }
        for mut b in std::mem::take(&mut self.frames[fi].dead_buffers) {
            b.destroy(&d, &mut self.core.alloc);
        }
        let dead_sets = std::mem::take(&mut self.frames[fi].dead_sets);
        for s in dead_sets {
            // find the pool the set came from: try them all (free fails harmlessly otherwise)
            for &pool in &self.desc_pools {
                if d.free_descriptor_sets(pool, &[s]).is_ok() {
                    break;
                }
            }
        }

        let (image_index, _) = match self.swapchain_fn.acquire_next_image(
            self.swapchain,
            u64::MAX,
            self.frames[fi].image_available,
            vk::Fence::null(),
        ) {
            Ok(r) => r,
            Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                self.swapchain_dirty = true;
                return Ok(());
            }
            Err(e) => return Err(format!("acquire: {e}")),
        };
        d.reset_fences(&[self.frames[fi].fence]).map_err(vkerr("reset fence"))?;

        // vertex data
        let verts = frame.vertices();
        let vbytes = std::slice::from_raw_parts(verts.as_ptr() as *const u8, std::mem::size_of_val(verts));
        if (self.frames[fi].vbuf.size as usize) < vbytes.len().max(16) {
            let mut old = std::mem::take(&mut self.frames[fi].vbuf);
            old.destroy(&d, &mut self.core.alloc);
            self.frames[fi].vbuf = Buffer::new(
                &d,
                &mut self.core.alloc,
                (vbytes.len() as u64).next_power_of_two().max(1 << 20),
                vk::BufferUsageFlags::VERTEX_BUFFER | vk::BufferUsageFlags::STORAGE_BUFFER,
                true,
            )?;
        }
        if !vbytes.is_empty() {
            self.frames[fi].vbuf.write(0, vbytes);
        }

        let cmd = self.frames[fi].cmd;
        d.reset_command_buffer(cmd, vk::CommandBufferResetFlags::empty()).map_err(vkerr("reset cmd"))?;
        d.begin_command_buffer(cmd, &vk::CommandBufferBeginInfo::default().flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT))
            .map_err(vkerr("begin cmd"))?;

        self.record_uploads(fi)?;

        let (vx, vy, vw, vh) = self.game_rect;

        // ray traced world
        let use_rt = self.rt_enabled && frame.has_camera != 0 && frame.cmds().iter().any(|c| c.flags & CMD_WORLD != 0);
        let mut rt_ready = false;
        if use_rt {
            if let Some(mut rtr) = self.rt.take() {
                let res = rtr.record(self, cmd, frame, vw, vh);
                self.rt = Some(rtr);
                match res {
                    Ok(traced) => rt_ready = traced,
                    Err(e) => {
                        self.window.log(2, &format!("ray tracing disabled: {e}"));
                        self.rt_enabled = false;
                    }
                }
            }
        }

        let clear = [
            vk::ClearValue { color: vk::ClearColorValue { float32: [0.0, 0.0, 0.0, 1.0] } },
            vk::ClearValue { depth_stencil: vk::ClearDepthStencilValue { depth: 1.0, stencil: 0 } },
        ];
        d.cmd_begin_render_pass(
            cmd,
            &vk::RenderPassBeginInfo::default()
                .render_pass(self.render_pass)
                .framebuffer(self.framebuffer)
                .render_area(vk::Rect2D { offset: vk::Offset2D { x: 0, y: 0 }, extent: self.sc_extent })
                .clear_values(&clear),
            vk::SubpassContents::INLINE,
        );
        // y flip via negative height (core since Vulkan 1.1)
        let viewport = vk::Viewport {
            x: vx as f32,
            y: (vy + vh as i32) as f32,
            width: vw as f32,
            height: -(vh as f32),
            min_depth: 0.0,
            max_depth: 1.0,
        };
        d.cmd_set_viewport(cmd, 0, &[viewport]);
        if !vbytes.is_empty() {
            d.cmd_bind_vertex_buffers(cmd, 0, &[self.frames[fi].vbuf.buffer], &[0]);
        }

        let gw = frame.vwidth();
        let sx = vw as f32 / gw;
        let sy = vh as f32 / 240.0;
        let mut bound: Option<usize> = None;
        let mut composited = false;
        for (ci, c) in frame.cmds().iter().enumerate() {
            let world = c.flags & CMD_WORLD != 0;
            if rt_ready && world && !composited {
                // composite the ray traced world at its place in draw order
                if let Some(rtr) = self.rt.as_ref() {
                    rtr.composite(&d, cmd, viewport);
                }
                composited = true;
                bound = None;
            }
            let _ = ci;
            if c.vertex_count == 0 {
                continue;
            }
            let blend = c.flags & CMD_BLEND != 0;
            let depth_test = rt_ready && world;
            if rt_ready && world && !blend {
                continue; // drawn by the ray tracer
            }
            let pi = (blend as usize) * 2 + depth_test as usize;
            if bound != Some(pi) {
                d.cmd_bind_pipeline(cmd, vk::PipelineBindPoint::GRAPHICS, self.pipelines[pi]);
                d.cmd_set_viewport(cmd, 0, &[viewport]);
                bound = Some(pi);
            }
            let x0 = vx + (c.scissor[0].max(0) as f32 * sx) as i32;
            let x1 = vx + ((c.scissor[2] as f32).min(gw) * sx).ceil() as i32;
            let y0 = vy + (c.scissor[1].max(0) as f32 * sy) as i32;
            let y1 = vy + (c.scissor[3].min(240) as f32 * sy).ceil() as i32;
            let sc = vk::Rect2D {
                offset: vk::Offset2D { x: x0.max(0), y: y0.max(0) },
                extent: vk::Extent2D { width: (x1 - x0).max(0) as u32, height: (y1 - y0).max(0) as u32 },
            };
            d.cmd_set_scissor(cmd, 0, &[sc]);
            let filter = c.flags & CMD_FILTER != 0;
            let s0 = self.descriptor_for(c.tex[0], sampler_index(filter, c.wrap[0]) as u8);
            let s1 = self.descriptor_for(c.tex[1], sampler_index(filter, c.wrap[1]) as u8);
            d.cmd_bind_descriptor_sets(cmd, vk::PipelineBindPoint::GRAPHICS, self.pipeline_layout, 0, &[s0, s1], &[]);
            let ccw = |i: usize| u32::from_le_bytes([c.cc[i], c.cc[i + 1], c.cc[i + 2], c.cc[i + 3]]);
            let pc = PushConstants {
                cc: [ccw(0), ccw(4), ccw(8), ccw(12)],
                colors: [pack_rgba(c.prim), pack_rgba(c.env), pack_rgba(c.fog), pack_rgba(c.blend)],
                flags: c.flags,
                plod: c.prim_lod_frac,
            };
            let pcb = std::slice::from_raw_parts(&pc as *const PushConstants as *const u8, std::mem::size_of::<PushConstants>());
            d.cmd_push_constants(cmd, self.pipeline_layout, vk::ShaderStageFlags::FRAGMENT, 0, pcb);
            d.cmd_draw(cmd, c.vertex_count, 1, c.first_vertex, 0);
        }
        d.cmd_end_render_pass(cmd);
        self.scene_layout = vk::ImageLayout::TRANSFER_SRC_OPTIMAL;

        // present: blit the scene into the swapchain image
        let sc_img = self.sc_images[image_index as usize];
        barrier(&d, cmd, sc_img, vk::ImageAspectFlags::COLOR, vk::ImageLayout::UNDEFINED,
                vk::ImageLayout::TRANSFER_DST_OPTIMAL);
        let full = |e: vk::Extent2D| [vk::Offset3D { x: 0, y: 0, z: 0 }, vk::Offset3D { x: e.width as i32, y: e.height as i32, z: 1 }];
        let sub = vk::ImageSubresourceLayers { aspect_mask: vk::ImageAspectFlags::COLOR, mip_level: 0, base_array_layer: 0, layer_count: 1 };
        let blit = vk::ImageBlit { src_subresource: sub, src_offsets: full(self.scene.extent), dst_subresource: sub, dst_offsets: full(self.sc_extent) };
        d.cmd_blit_image(cmd, self.scene.image, vk::ImageLayout::TRANSFER_SRC_OPTIMAL, sc_img,
                         vk::ImageLayout::TRANSFER_DST_OPTIMAL, &[blit], vk::Filter::NEAREST);
        barrier(&d, cmd, sc_img, vk::ImageAspectFlags::COLOR, vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                vk::ImageLayout::PRESENT_SRC_KHR);
        d.end_command_buffer(cmd).map_err(vkerr("end cmd"))?;

        let wait = [self.frames[fi].image_available];
        let stages = [vk::PipelineStageFlags::TRANSFER];
        let signal = [self.sc_render_done[image_index as usize]];
        let cmds = [cmd];
        let submit = vk::SubmitInfo::default()
            .wait_semaphores(&wait)
            .wait_dst_stage_mask(&stages)
            .command_buffers(&cmds)
            .signal_semaphores(&signal);
        d.queue_submit(self.core.queue, &[submit], self.frames[fi].fence).map_err(vkerr("submit"))?;
        let swapchains = [self.swapchain];
        let indices = [image_index];
        let present = vk::PresentInfoKHR::default()
            .wait_semaphores(&signal)
            .swapchains(&swapchains)
            .image_indices(&indices);
        match self.swapchain_fn.queue_present(self.core.queue, &present) {
            Ok(false) => {}
            Ok(true) | Err(vk::Result::ERROR_OUT_OF_DATE_KHR) => self.swapchain_dirty = true,
            Err(e) => return Err(format!("present: {e}")),
        }
        self.frame_idx = (self.frame_idx + 1) % FRAMES;
        Ok(())
    }
}

impl Backend for VkBackend {
    fn texture_create(&mut self, w: u32, h: u32, rgba: &[u8]) -> u32 {
        let img = match Image::new(
            &self.core.device,
            &mut self.core.alloc,
            w,
            h,
            vk::Format::R8G8B8A8_UNORM,
            vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::TRANSFER_DST,
            vk::ImageAspectFlags::COLOR,
        ) {
            Ok(i) => i,
            Err(e) => {
                self.window.log(2, &e);
                return 0;
            }
        };
        let id = if let Some(id) = self.free_ids.pop() {
            self.textures[id as usize] = Some(Texture { image: img, sets: Vec::new() });
            id
        } else {
            self.textures.push(Some(Texture { image: img, sets: Vec::new() }));
            (self.textures.len() - 1) as u32
        };
        self.pending.push(PendingUpload { id, w, h, data: rgba.to_vec() });
        id
    }

    fn texture_destroy(&mut self, id: u32) {
        if id == self.white || id == 0 {
            return;
        }
        let Some(slot) = self.textures.get_mut(id as usize) else { return };
        let Some(tex) = slot.take() else { return };
        self.pending.retain(|p| p.id != id);
        self.desc_cache.retain(|k, _| k.0 != id);
        // the previous frame may still sample it: free after this slot's fence
        let fi = self.frame_idx;
        self.frames[fi].dead_images.push(tex.image);
        self.frames[fi].dead_sets.extend(tex.sets);
        self.free_ids.push(id);
    }

    fn render(&mut self, frame: &D64GfxFrame) {
        if let Err(e) = unsafe { self.render_inner(frame) } {
            self.window.log(2, &format!("Vulkan frame failed: {e}"));
            self.swapchain_dirty = true;
        }
    }

    fn set_raytracing(&mut self, on: bool) {
        if let Some(r) = self.rt.as_mut() {
            r.reset_history();
        }
        if !on {
            self.rt_enabled = false;
            return;
        }
        if !self.rt_supported {
            return;
        }
        if self.rt.is_none() {
            match unsafe { rt::RayTracer::new(self) } {
                Ok(r) => self.rt = Some(r),
                Err(e) => {
                    self.window.log(2, &format!("ray tracing init failed: {e}"));
                    self.rt_supported = false;
                    return;
                }
            }
        }
        self.rt_enabled = true;
    }

    fn set_vsync(&mut self, on: bool) {
        if self.vsync != on {
            self.vsync = on;
            self.swapchain_dirty = true;
        }
    }

    fn rt_supported(&self) -> bool {
        self.rt_supported
    }
    fn rt_enabled(&self) -> bool {
        self.rt_enabled
    }
    fn device_name(&self) -> String {
        self.device_name.clone()
    }
    fn backend_name(&self) -> &'static str {
        "Vulkan"
    }

    fn set_rt_params(&mut self, p: RtParams) {
        self.rt_params = p;
    }

    fn read_frame(&mut self, dst: &mut [u8], w: u32, h: u32) {
        unsafe {
            let d = self.core.device.clone();
            if self.scene_layout != vk::ImageLayout::TRANSFER_SRC_OPTIMAL {
                return;
            }
            d.device_wait_idle().ok();
            let (vx, vy, vw, vh) = self.game_rect;
            let size = (vw * vh * 4) as u64;
            if self.readback.size < size {
                let mut old = std::mem::take(&mut self.readback);
                old.destroy(&d, &mut self.core.alloc);
                match Buffer::new(&d, &mut self.core.alloc, size, vk::BufferUsageFlags::TRANSFER_DST, true) {
                    Ok(b) => self.readback = b,
                    Err(_) => return,
                }
            }
            let cmd = self.frames[self.frame_idx].cmd;
            d.reset_command_buffer(cmd, vk::CommandBufferResetFlags::empty()).ok();
            d.begin_command_buffer(cmd, &vk::CommandBufferBeginInfo::default().flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT)).ok();
            let region = vk::BufferImageCopy::default()
                .image_subresource(vk::ImageSubresourceLayers { aspect_mask: vk::ImageAspectFlags::COLOR, mip_level: 0, base_array_layer: 0, layer_count: 1 })
                .image_offset(vk::Offset3D { x: vx, y: vy, z: 0 })
                .image_extent(vk::Extent3D { width: vw, height: vh, depth: 1 });
            d.cmd_copy_image_to_buffer(cmd, self.scene.image, vk::ImageLayout::TRANSFER_SRC_OPTIMAL, self.readback.buffer, &[region]);
            d.end_command_buffer(cmd).ok();
            let cmds = [cmd];
            let fence = d.create_fence(&vk::FenceCreateInfo::default(), None).unwrap();
            d.queue_submit(self.core.queue, &[vk::SubmitInfo::default().command_buffers(&cmds)], fence).ok();
            d.wait_for_fences(&[fence], true, u64::MAX).ok();
            d.destroy_fence(fence, None);
            let src = std::slice::from_raw_parts(self.readback.alloc.mapped, size as usize);
            rescale_rgba(src, vw, vh, dst, w, h, false);
        }
    }
}

impl Drop for VkBackend {
    fn drop(&mut self) {
        unsafe {
            let d = self.core.device.clone();
            d.device_wait_idle().ok();
            if let Some(mut r) = self.rt.take() {
                r.destroy(self);
            }
            for t in self.textures.drain(..).flatten() {
                let mut img = t.image;
                img.destroy(&d, &mut self.core.alloc);
            }
            for mut f in self.frames.drain(..) {
                for mut i in f.dead_images.drain(..) {
                    i.destroy(&d, &mut self.core.alloc);
                }
                for mut b in f.dead_buffers.drain(..) {
                    b.destroy(&d, &mut self.core.alloc);
                }
                f.vbuf.destroy(&d, &mut self.core.alloc);
                f.staging.destroy(&d, &mut self.core.alloc);
                d.destroy_fence(f.fence, None);
                d.destroy_semaphore(f.image_available, None);
            }
            let mut rb = std::mem::take(&mut self.readback);
            rb.destroy(&d, &mut self.core.alloc);
            self.destroy_swapchain_resources();
            for p in self.desc_pools.drain(..) {
                d.destroy_descriptor_pool(p, None);
            }
            for s in self.samplers.drain(..) {
                d.destroy_sampler(s, None);
            }
            for p in self.pipelines {
                d.destroy_pipeline(p, None);
            }
            d.destroy_pipeline_layout(self.pipeline_layout, None);
            d.destroy_descriptor_set_layout(self.dsl_tex, None);
            d.destroy_render_pass(self.render_pass, None);
            d.destroy_command_pool(self.cmd_pool, None);
            self.core.alloc.destroy(&d);
            d.destroy_device(None);
            self.surface_fn.destroy_surface(self.surface, None);
            self.core.instance.destroy_instance(None);
        }
    }
}
