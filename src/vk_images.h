#pragma once 

#include <vulkan/vulkan.h>

namespace vkutil {

	void transition_image(VkCommandBuffer cmdBfr, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

	void copy_image_to_image(VkCommandBuffer cmdBfr, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
};