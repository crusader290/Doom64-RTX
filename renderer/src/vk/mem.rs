//! Minimal device memory sub-allocator.
//!
//! The texture cache can hold thousands of small images; giving each its own
//! VkDeviceMemory would run into maxMemoryAllocationCount (4096 on many
//! drivers). Allocations are carved out of 32 MiB blocks per memory type with
//! a first-fit free list. Large requests get a dedicated block.

use ash::vk;

const BLOCK_SIZE: vk::DeviceSize = 32 * 1024 * 1024;

#[derive(Clone, Copy, Debug)]
pub struct Allocation {
    pub memory: vk::DeviceMemory,
    pub offset: vk::DeviceSize,
    pub size: vk::DeviceSize,
    pub mapped: *mut u8,
    block: usize,
}

impl Default for Allocation {
    fn default() -> Self {
        Allocation { memory: vk::DeviceMemory::null(), offset: 0, size: 0, mapped: std::ptr::null_mut(), block: usize::MAX }
    }
}

struct Block {
    memory: vk::DeviceMemory,
    size: vk::DeviceSize,
    type_index: u32,
    device_address: bool,
    mapped: *mut u8,
    free: Vec<(vk::DeviceSize, vk::DeviceSize)>, // (offset, size), sorted by offset
    live: usize,
}

pub struct Allocator {
    props: vk::PhysicalDeviceMemoryProperties,
    blocks: Vec<Option<Block>>,
    pub supports_device_address: bool,
}

fn align_up(v: vk::DeviceSize, a: vk::DeviceSize) -> vk::DeviceSize {
    if a <= 1 {
        v
    } else {
        v.div_ceil(a) * a
    }
}

impl Allocator {
    pub fn new(props: vk::PhysicalDeviceMemoryProperties, supports_device_address: bool) -> Self {
        Allocator { props, blocks: Vec::new(), supports_device_address }
    }

    pub fn find_type(&self, bits: u32, flags: vk::MemoryPropertyFlags) -> Option<u32> {
        (0..self.props.memory_type_count).find(|&i| {
            bits & (1 << i) != 0 && self.props.memory_types[i as usize].property_flags.contains(flags)
        })
    }

    /// `host` selects HOST_VISIBLE|HOST_COHERENT memory (mapped persistently);
    /// otherwise DEVICE_LOCAL is preferred.
    pub fn alloc(
        &mut self,
        device: &ash::Device,
        req: vk::MemoryRequirements,
        host: bool,
        device_address: bool,
    ) -> Result<Allocation, String> {
        let wanted = if host {
            vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT
        } else {
            vk::MemoryPropertyFlags::DEVICE_LOCAL
        };
        let type_index = self
            .find_type(req.memory_type_bits, wanted)
            .or_else(|| if host { None } else { self.find_type(req.memory_type_bits, vk::MemoryPropertyFlags::empty()) })
            .ok_or("no suitable Vulkan memory type")?;

        // try existing blocks
        for (bi, slot) in self.blocks.iter_mut().enumerate() {
            let Some(b) = slot else { continue };
            if b.type_index != type_index || b.device_address != device_address {
                continue;
            }
            for fi in 0..b.free.len() {
                let (off, sz) = b.free[fi];
                let start = align_up(off, req.alignment);
                if start + req.size <= off + sz {
                    // split the free range
                    let head = start - off;
                    let tail = (off + sz) - (start + req.size);
                    b.free.remove(fi);
                    if tail > 0 {
                        b.free.insert(fi, (start + req.size, tail));
                    }
                    if head > 0 {
                        b.free.insert(fi, (off, head));
                    }
                    b.live += 1;
                    return Ok(Allocation {
                        memory: b.memory,
                        offset: start,
                        size: req.size,
                        mapped: if b.mapped.is_null() { b.mapped } else { unsafe { b.mapped.add(start as usize) } },
                        block: bi,
                    });
                }
            }
        }

        // new block
        let size = req.size.max(BLOCK_SIZE);
        let mut flags_info = vk::MemoryAllocateFlagsInfo::default().flags(vk::MemoryAllocateFlags::DEVICE_ADDRESS);
        let mut info = vk::MemoryAllocateInfo::default().allocation_size(size).memory_type_index(type_index);
        if device_address {
            info = info.push_next(&mut flags_info);
        }
        let memory = unsafe { device.allocate_memory(&info, None) }.map_err(|e| format!("vkAllocateMemory: {e}"))?;
        let host_visible = self.props.memory_types[type_index as usize]
            .property_flags
            .contains(vk::MemoryPropertyFlags::HOST_VISIBLE);
        let mapped = if host_visible {
            unsafe { device.map_memory(memory, 0, vk::WHOLE_SIZE, vk::MemoryMapFlags::empty()) }
                .map_err(|e| format!("vkMapMemory: {e}"))? as *mut u8
        } else {
            std::ptr::null_mut()
        };
        let mut free = Vec::new();
        if size > req.size {
            free.push((req.size, size - req.size));
        }
        let block = Block { memory, size, type_index, device_address, mapped, free, live: 1 };
        let bi = match self.blocks.iter().position(|b| b.is_none()) {
            Some(i) => {
                self.blocks[i] = Some(block);
                i
            }
            None => {
                self.blocks.push(Some(block));
                self.blocks.len() - 1
            }
        };
        Ok(Allocation { memory, offset: 0, size: req.size, mapped, block: bi })
    }

    pub fn free(&mut self, device: &ash::Device, a: Allocation) {
        let Some(Some(b)) = self.blocks.get_mut(a.block) else { return };
        // insert and coalesce
        let pos = b.free.iter().position(|&(o, _)| o > a.offset).unwrap_or(b.free.len());
        b.free.insert(pos, (a.offset, a.size));
        let mut i = 0;
        while i + 1 < b.free.len() {
            let (o, s) = b.free[i];
            let (o2, s2) = b.free[i + 1];
            if o + s == o2 {
                b.free[i] = (o, s + s2);
                b.free.remove(i + 1);
            } else {
                i += 1;
            }
        }
        b.live -= 1;
        if b.live == 0 && b.size > BLOCK_SIZE {
            // dedicated oversized block: release it
            unsafe { device.free_memory(b.memory, None) };
            self.blocks[a.block] = None;
        }
    }

    pub fn destroy(&mut self, device: &ash::Device) {
        for b in self.blocks.drain(..).flatten() {
            unsafe { device.free_memory(b.memory, None) };
        }
    }
}

/// Buffer plus its memory.
#[derive(Default)]
pub struct Buffer {
    pub buffer: vk::Buffer,
    pub alloc: Allocation,
    pub size: vk::DeviceSize,
    pub address: vk::DeviceAddress,
}

impl Buffer {
    pub fn new(
        device: &ash::Device,
        alloc: &mut Allocator,
        size: vk::DeviceSize,
        usage: vk::BufferUsageFlags,
        host: bool,
    ) -> Result<Buffer, String> {
        let size = size.max(16);
        let da = usage.contains(vk::BufferUsageFlags::SHADER_DEVICE_ADDRESS);
        let info = vk::BufferCreateInfo::default().size(size).usage(usage).sharing_mode(vk::SharingMode::EXCLUSIVE);
        let buffer = unsafe { device.create_buffer(&info, None) }.map_err(|e| format!("vkCreateBuffer: {e}"))?;
        let req = unsafe { device.get_buffer_memory_requirements(buffer) };
        let a = alloc.alloc(device, req, host, da)?;
        unsafe { device.bind_buffer_memory(buffer, a.memory, a.offset) }.map_err(|e| format!("bind buffer: {e}"))?;
        let address = if da {
            unsafe { device.get_buffer_device_address(&vk::BufferDeviceAddressInfo::default().buffer(buffer)) }
        } else {
            0
        };
        Ok(Buffer { buffer, alloc: a, size, address })
    }

    pub fn destroy(&mut self, device: &ash::Device, alloc: &mut Allocator) {
        if self.buffer != vk::Buffer::null() {
            unsafe { device.destroy_buffer(self.buffer, None) };
            alloc.free(device, self.alloc);
            self.buffer = vk::Buffer::null();
        }
    }

    /// Copies bytes into a host visible buffer.
    pub fn write(&self, offset: usize, data: &[u8]) {
        assert!(!self.alloc.mapped.is_null());
        assert!(offset + data.len() <= self.size as usize);
        unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), self.alloc.mapped.add(offset), data.len()) };
    }
}

/// Image plus its memory and default view.
#[derive(Default)]
pub struct Image {
    pub image: vk::Image,
    pub view: vk::ImageView,
    pub alloc: Allocation,
    pub format: vk::Format,
    pub extent: vk::Extent2D,
}

impl Image {
    pub fn new(
        device: &ash::Device,
        alloc: &mut Allocator,
        w: u32,
        h: u32,
        format: vk::Format,
        usage: vk::ImageUsageFlags,
        aspect: vk::ImageAspectFlags,
    ) -> Result<Image, String> {
        let info = vk::ImageCreateInfo::default()
            .image_type(vk::ImageType::TYPE_2D)
            .format(format)
            .extent(vk::Extent3D { width: w, height: h, depth: 1 })
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            .usage(usage)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED);
        let image = unsafe { device.create_image(&info, None) }.map_err(|e| format!("vkCreateImage: {e}"))?;
        let req = unsafe { device.get_image_memory_requirements(image) };
        let a = alloc.alloc(device, req, false, false)?;
        unsafe { device.bind_image_memory(image, a.memory, a.offset) }.map_err(|e| format!("bind image: {e}"))?;
        let view_info = vk::ImageViewCreateInfo::default()
            .image(image)
            .view_type(vk::ImageViewType::TYPE_2D)
            .format(format)
            .subresource_range(vk::ImageSubresourceRange {
                aspect_mask: aspect,
                base_mip_level: 0,
                level_count: 1,
                base_array_layer: 0,
                layer_count: 1,
            });
        let view = unsafe { device.create_image_view(&view_info, None) }.map_err(|e| format!("vkCreateImageView: {e}"))?;
        Ok(Image { image, view, alloc: a, format, extent: vk::Extent2D { width: w, height: h } })
    }

    pub fn destroy(&mut self, device: &ash::Device, alloc: &mut Allocator) {
        if self.image != vk::Image::null() {
            unsafe {
                device.destroy_image_view(self.view, None);
                device.destroy_image(self.image, None);
            }
            alloc.free(device, self.alloc);
            self.image = vk::Image::null();
        }
    }
}

/// Records a simple layout transition with conservative stage masks.
pub fn barrier(
    device: &ash::Device,
    cmd: vk::CommandBuffer,
    image: vk::Image,
    aspect: vk::ImageAspectFlags,
    old: vk::ImageLayout,
    new: vk::ImageLayout,
) {
    let b = vk::ImageMemoryBarrier::default()
        .old_layout(old)
        .new_layout(new)
        .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .image(image)
        .subresource_range(vk::ImageSubresourceRange {
            aspect_mask: aspect,
            base_mip_level: 0,
            level_count: 1,
            base_array_layer: 0,
            layer_count: 1,
        })
        .src_access_mask(vk::AccessFlags::MEMORY_WRITE)
        .dst_access_mask(vk::AccessFlags::MEMORY_READ | vk::AccessFlags::MEMORY_WRITE);
    unsafe {
        device.cmd_pipeline_barrier(
            cmd,
            vk::PipelineStageFlags::ALL_COMMANDS,
            vk::PipelineStageFlags::ALL_COMMANDS,
            vk::DependencyFlags::empty(),
            &[],
            &[],
            &[b],
        )
    };
}
