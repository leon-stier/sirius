//
// Created by Leon on 05/11/2026.
//

#include "vulkanUtils.h"

#include "vulkanContext.h"

namespace sirius {
std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VulkanUtils::CreateBuffer(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, vk::MemoryPropertyFlags memoryProperties) {
    const vk::BufferCreateInfo bufferInfo{
        .size = size,
        .usage = bufferUsage,
        .sharingMode = vk::SharingMode::eExclusive
    };
    auto buffer = vk::raii::Buffer(context.Device(), bufferInfo);

    const vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateInfo memoryAllocateInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = VulkanUtils::FindMemoryType(context, memRequirements.memoryTypeBits, memoryProperties)
    };
    auto bufferMemory{vk::raii::DeviceMemory(context.Device(), memoryAllocateInfo)};
    buffer.bindMemory(bufferMemory, 0);

    return {std::move(buffer), std::move(bufferMemory)};
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanUtils::CreateImage(VulkanContext& context, uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) {
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    auto image = vk::raii::Image(context.Device(), imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = FindMemoryType(context, memRequirements.memoryTypeBits, properties)
    };
    auto imageMemory = vk::raii::DeviceMemory(context.Device(), allocInfo);
    image.bindMemory(imageMemory, 0);

    return {std::move(image), std::move(imageMemory)};
}

vk::raii::ImageView VulkanUtils::CreateImageView(VulkanContext& context, vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspectFlags) {
    const vk::ImageViewCreateInfo viewCreateInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    return vk::raii::ImageView(context.Device(), viewCreateInfo);
}

void VulkanUtils::TransitionImageLayout(vk::Image image, const vk::raii::CommandBuffer& commandBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask, vk::ImageAspectFlags imageAspectFlags) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = {
            .aspectMask = imageAspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    const vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    commandBuffer.pipelineBarrier2(dependencyInfo);
}

/*
 * Find a memory type for the buffer that is both supported and works for our application
 * typeFilter specifies suitable memory types by setting their corresponding bit to 1
 * So to find the indices of suitable types, we just iterate over all and check if their bit is set to 1
 * memoryProperties.MemoryTypes contains vk::MemoryType structs that specify the heap and properties of each memory type
 * We check those against the specified properties we need for the buffer
 * So only a type that's suitable (bit set to 1) and fits our required properties is selected
 */
uint32_t VulkanUtils::FindMemoryType(VulkanContext& context, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    const vk::PhysicalDeviceMemoryProperties2 memoryProperties{context.PhysicalDevice().getMemoryProperties2()};

    for (uint32_t i = 0; i < memoryProperties.memoryProperties.memoryTypeCount; i++) {
        if (typeFilter & (1 << i) && (memoryProperties.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("No suitable memory type found");
}

vk::raii::CommandBuffer VulkanUtils::BeginSingleTimeCommands(VulkanContext& context, const vk::raii::CommandPool& commandPool) {
    const vk::CommandBufferAllocateInfo allocateInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer commandBuffer = std::move(context.Device().allocateCommandBuffers(allocateInfo).front());

    commandBuffer.begin({
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    return std::move(commandBuffer);
}

void VulkanUtils::EndSingleTimeCommands(VulkanContext& context, vk::raii::CommandBuffer&& commandBuffer) {
    commandBuffer.end();

    const vk::CommandBufferSubmitInfo commandBufferSubmitInfo{
        .commandBuffer = *commandBuffer
    };

    const vk::SubmitInfo2 submitInfo{
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmitInfo
    };

    context.GraphicsQueue().submit2(submitInfo, nullptr);
    context.GraphicsQueue().waitIdle();
}
}
