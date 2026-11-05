//
// Created by Leon on 05/11/2026.
//

#pragma once
#include <vulkan/vulkan_raii.hpp>


class VulkanContext;

namespace sirius {
class VulkanUtils {
public:
    static std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties);

    static void CopyBuffer(VulkanContext& context, const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size);


    static std::pair<vk::raii::Image, vk::raii::DeviceMemory> CreateImage(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

    static vk::raii::ImageView CreateImageView(VulkanContext& context, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspectFlags);

    static void TransitionImageLayout(
        vk::Image image,
        const vk::raii::CommandBuffer& commandBuffer,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask,
        vk::ImageAspectFlags imageAspectFlags
    );

    static uint32_t FindMemoryType(VulkanContext& context, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    static vk::raii::CommandBuffer BeginSingleTimeCommands(VulkanContext& context, const vk::raii::CommandPool& commandPool);

    static void EndSingleTimeCommands(VulkanContext& context, vk::raii::CommandBuffer&& commandBuffer);
};
}
