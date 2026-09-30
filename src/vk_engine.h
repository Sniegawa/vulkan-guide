// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_initializers.h>
#include <vulkan/vulkan_core.h>

#include "VkBootstrap.h"
#include "vk_descriptors.h"

struct FrameData
{
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    DeletionQueue _deletionQueue;
};

struct ComputePushConstants {
    glm::vec4 data1 = glm::vec4(0.0f);
    glm::vec4 data2 = glm::vec4(0.0f);
    glm::vec4 data3 = glm::vec4(0.0f);
    glm::vec4 data4 = glm::vec4(0.0f);
};

const unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkSurfaceKHR _surface;
    
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;

    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;

    FrameData _frames[FRAME_OVERLAP];

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;

    DescriptorAllocator globalDescriptorAllocator;

    VkDescriptorSet _DrawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    // ------ PIPELINES ------ 
    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;


    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;
    

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

    void draw_background(VkCommandBuffer cmd);

    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	//run main loop
	void run();

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();
    void init_descriptors();

    void init_pipelines();
    void init_background_pipelines();

    void init_imgui();

    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();

    FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }

private:
    DeletionQueue _mainDeletionQueue;

    VmaAllocator _allocator;

    AllocatedImage _drawImage;
    VkExtent2D _drawExtent;
};
