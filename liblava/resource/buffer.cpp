// file      : liblava/resource/buffer.cpp
// copyright : Copyright (c) 2018-present, Lava Block OÜ
// license   : MIT; see accompanying LICENSE file

#include <liblava/resource/buffer.hpp>

namespace lava {

VkPipelineStageFlags buffer::usage_to_possible_stages(VkBufferUsageFlags usage) {

    VkPipelineStageFlags flags = 0;

    if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
        flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    return flags;
}

VkAccessFlags buffer::usage_to_possible_access(VkBufferUsageFlags usage) {

    VkAccessFlags flags = 0;

    if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        flags |= VK_ACCESS_INDEX_READ_BIT;
    if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        flags |= VK_ACCESS_UNIFORM_READ_BIT;
    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    return flags;
}

buffer::buffer() : _id(ids::next()) {}

buffer::~buffer() {

    ids::free(_id);

    destroy();
}

bool buffer::create(device* device, void const* data, size_t size, VkBufferUsageFlags usage, bool mapped, VmaMemoryUsage memoryUsage) {

    dev = device;

    VkBufferCreateInfo buffer_info
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
    };

    VmaAllocationCreateFlags const alloc_flags = mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0;

    VmaAllocationCreateInfo alloc_info
    {
        .flags = alloc_flags,
        .usage = memoryUsage,
    };

    if (failed(vmaCreateBuffer(dev->alloc(), &buffer_info, &alloc_info, &vk_buffer, &allocation, &allocation_info))) {

        log()->error("buffer::create vmaCreateBuffer failed");
        return false;
    }

    if (!mapped) {

        data_ptr map = nullptr;
        if (failed(vmaMapMemory(dev->alloc(), allocation, (void**)(&map)))) {

            log()->error("buffer::create vmaMapMemory failed");
            return false;
        }

        memcpy(map, data, size);

        vmaUnmapMemory(dev->alloc(), allocation);

    } else if (data) {

        memcpy(allocation_info.pMappedData, data, size);

        flush();
    }

    descriptor.buffer = vk_buffer;
    descriptor.offset = 0;
    descriptor.range = size;

    return true;
}

void buffer::destroy() {

    if (!vk_buffer)
        return;

    vmaDestroyBuffer(dev->alloc(), vk_buffer, allocation);
    vk_buffer = nullptr;
    allocation = nullptr;

    dev = nullptr;
}

void buffer::flush(VkDeviceSize offset, VkDeviceSize size) {

    vmaFlushAllocation(dev->alloc(), allocation, offset, size);
}

} // lava
