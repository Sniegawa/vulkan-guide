// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include "DeletionQueue.h"

struct FrameData {
	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	VkSemaphore swapchainSemaphore;
	VkFence renderFence;

	DeletionQueue _dQueue;
};

struct SwapchainData {
	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;

	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;

	std::vector<VkSemaphore> imageAvailableSemaphores; // One per swapchain
	std::vector<VkSemaphore> freeSemaphores; // pool of unowned semaphores
	std::vector<VkSemaphore> imageOwnerSemaphores; // indexed by swapchainImageIndex

	std::vector<VkSemaphore> renderSemaphores; 

	VkExtent2D swapchainExtent;

	DeletionQueue _dQueue;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as the default device -- Propably should rename that
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface; // 

	SwapchainData _swapchainData;

	FrameData _frames[FRAME_OVERLAP]; // Should be privated

	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();

	void create_swapchain(uint32_t width, uint32_t height);
private:
	DeletionQueue m_mainDeletionQueue;
};
