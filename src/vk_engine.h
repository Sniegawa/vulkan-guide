// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>

#include <vk_descriptors.h>
#include <vk_pipelines.h>
#include <vk_loader.h>

constexpr unsigned int FRAME_OVERLAP = 2;

struct ComputePushConstants 
{
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

class VulkanEngine {
public:

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as the default device -- Propably should rename that
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface; // the drawing surface

	SwapchainData _swapchainData;

	FrameData _frames[FRAME_OVERLAP]; // Should be privated

	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VmaAllocator _allocator;

	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;


	DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	//VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;



	VkFence _immFence; // Immediate submit fence
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1600 , 900 };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();
	void draw_imgui(VkCommandBuffer cmdBfr, VkImageView targetImageView);

	//run main loop
	void run();

	void draw_background(VkCommandBuffer cmdBfr);
	void draw_geometry(VkCommandBuffer cmdBfr);

	void immediate_submit(std::function<void(VkCommandBuffer cmdBfr)>&& function);
	GPUMeshBuffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

private:
	void init_default_data();

	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void init_descriptors();
	void init_imgui();

	void init_pipelines();
	void init_background_pipelines();
	void init_mesh_pipeline();

	void create_swapchain(uint32_t width, uint32_t height);
	
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);


private:
	DeletionQueue m_mainDeletionQueue;

	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	int currentMesh = 0;


	float _deltaTime = 0.0f;
	std::chrono::time_point<std::chrono::high_resolution_clock> _lastFrameTime;
};
