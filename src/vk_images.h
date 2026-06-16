#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {

	void transition_image(VkCommandBuffer cmdBfr, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
};