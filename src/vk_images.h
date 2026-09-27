#pragma once 

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

namespace vkutil {

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

};
