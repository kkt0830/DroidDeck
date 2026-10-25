#ifndef DROIDDECK_GPU_READBACK_H
#define DROIDDECK_GPU_READBACK_H

#include <stdint.h>

static uint32_t probe_memory_type(const VkPhysicalDeviceMemoryProperties *memory,
                                  uint32_t bits, VkMemoryPropertyFlags required) {
    for (uint32_t i = 0; i < memory->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memory->memoryTypes[i].propertyFlags & required) == required) return i;
    return UINT32_MAX;
}

static int probe_gpu_readback(VkPhysicalDevice physical, VkDevice device, uint32_t family) {
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE, buffer_memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int ok = 0;
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
#define GPU_CHECK(gate, call) do { if (!report(gate, (call))) goto cleanup; } while (0)
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {2, 2, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    GPU_CHECK("gpu_image_create", vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    uint32_t type = probe_memory_type(&memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) type = probe_memory_type(&memory, requirements.memoryTypeBits, 0);
    if (type == UINT32_MAX) { report("gpu_image_memory_type", VK_ERROR_FEATURE_NOT_PRESENT); goto cleanup; }
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    GPU_CHECK("gpu_image_allocate", vkAllocateMemory(device, &allocation, NULL, &image_memory));
    GPU_CHECK("gpu_image_bind", vkBindImageMemory(device, image, image_memory, 0));
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 16, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    GPU_CHECK("gpu_buffer_create", vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    type = probe_memory_type(&memory, requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) type = probe_memory_type(&memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type == UINT32_MAX) { report("gpu_buffer_memory_type", VK_ERROR_FEATURE_NOT_PRESENT); goto cleanup; }
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    GPU_CHECK("gpu_buffer_allocate", vkAllocateMemory(device, &allocation, NULL, &buffer_memory));
    GPU_CHECK("gpu_buffer_bind", vkBindBufferMemory(device, buffer, buffer_memory, 0));
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family};
    GPU_CHECK("gpu_command_pool", vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    GPU_CHECK("gpu_command_allocate", vkAllocateCommandBuffers(device, &commands, &cmd));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    GPU_CHECK("gpu_command_begin", vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkClearColorValue red = {.float32 = {1, 0, 0, 1}};
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &red, 1, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {2, 2, 1}};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer, .offset = 0, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, NULL, 1, &host, 0, NULL);
    GPU_CHECK("gpu_command_end", vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    GPU_CHECK("gpu_fence_create", vkCreateFence(device, &fence_info, NULL, &fence));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    GPU_CHECK("gpu_queue_submit", vkQueueSubmit(queue, 1, &submit, fence));
    VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull);
    if (!report("gpu_fence_wait", waited)) {
        printf("panvk_probe.gpu_cleanup=process-exit (work may still be pending)\n");
        fflush(stdout);
        _Exit(2);
    }
    void *pixels = NULL;
    GPU_CHECK("gpu_buffer_map", vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &pixels));
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = buffer_memory, .offset = 0, .size = VK_WHOLE_SIZE};
    VkResult invalidated = vkInvalidateMappedMemoryRanges(device, 1, &range);
    int matches = invalidated == VK_SUCCESS;
    unsigned char expected[] = {255, 0, 0, 255};
    if (invalidated == VK_SUCCESS) for (unsigned i = 0; i < 16; i++)
        if (((unsigned char *)pixels)[i] != expected[i % 4]) matches = 0;
    report("gpu_buffer_invalidate", invalidated);
    printf("panvk_probe.gpu_clear_readback=%s\n", matches ? "pass" : "fail");
    if (!matches && invalidated == VK_SUCCESS) {
        unsigned char *p = pixels;
        printf("panvk_probe.first_pixel=%u,%u,%u,%u expected=255,0,0,255\n", p[0], p[1], p[2], p[3]);
    }
    vkUnmapMemory(device, buffer_memory);
    ok = matches;
cleanup:
    if (fence) vkDestroyFence(device, fence, NULL);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (buffer) vkDestroyBuffer(device, buffer, NULL);
    if (buffer_memory) vkFreeMemory(device, buffer_memory, NULL);
    if (image) vkDestroyImage(device, image, NULL);
    if (image_memory) vkFreeMemory(device, image_memory, NULL);
#undef GPU_CHECK
    return ok;
}
#endif
