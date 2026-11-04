// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <string>
#include <vk_types.h>
#include <vk_initializers.h>
#include <vulkan/vulkan_core.h>

#include "vk_loader.h"
#include "camera.h"
#include "glm/ext/vector_float4.hpp"
#include "vk_descriptors.h"

struct EngineStats
{
    float frametime;
    int triangle_count;
    int drawcall_count;
    float scene_update_time;
    float mesh_draw_time;
    float fps;
};

struct GLTFMetallic_Roughness
{
    MaterialPipeline opaquePipeline;
    MaterialPipeline transparentPipeline;

    VkDescriptorSetLayout materialLayout;

    struct MaterialConstants
    {
        glm::vec4 colorFactors;
        glm::vec4 metal_rough_factors;
        // Padding because this structure will be bound to an Uniform Buffer, and in vulkan
        // Binding an Uniform Buffer needs to meet a minimum requirement for alignment.
        // 256 bytes which we get with this pad is a good default alignment
        glm::vec4 pad[14];
    };

    struct MaterialResources
    {
        AllocatedImage colorImage;
        VkSampler colorSampler;
        AllocatedImage metalRoughImage;
        VkSampler metalRoughSampler;
        VkBuffer dataBuffer;
        uint32_t dataBufferOffset;
    };

    DescriptorWriter writer;

    void build_pipelines(VulkanEngine* engine);
    void clear_resources(VkDevice device);

    MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descirptorAllocator);
};

struct FrameData
{
    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    DeletionQueue _deletionQueue;
    DescriptorAllocatorGrowable _frameDescriptors;
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

    DescriptorAllocatorGrowable globalDescriptorAllocator;

    VkDescriptorSet _DrawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;

    GPUSceneData sceneData;
    VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

    VkDescriptorSetLayout _singleImageDescriptorLayout;


    AllocatedImage _drawImage;
    AllocatedImage _depthImage;
    VkExtent2D _drawExtent;

    AllocatedImage _whiteImage, _blackImage, _greyImage, _errorCheckerboardImage;

    VkSampler _defaultSamplerLinear, _defaultSamplerNearest;


    MaterialInstance defaultData;
    GLTFMetallic_Roughness metalRoughMaterial;

    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

    // ------ PIPELINES ------ 
    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;

    VkPipelineLayout _meshPipelineLayout;
    VkPipeline _meshPipeline;

    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;
    

    EngineStats stats;

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

    void draw_geometry(VkCommandBuffer cmd);
    void draw_background(VkCommandBuffer cmd);

    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	//run main loop
	void run();

    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

    AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    void destroy_image(const AllocatedImage& image);

    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags = 0);

    void destroy_buffer(const AllocatedBuffer& buffer);
private:
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync_structures();
    void init_descriptors();

    void init_pipelines();
    void init_background_pipelines();
    void init_mesh_pipeline();

    void init_default_data();

    void init_imgui();

    void resize_swapchain();
    void create_swapchain(uint32_t width, uint32_t height);
    void destroy_swapchain();




    FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }

    void update_scene();

private:
    DeletionQueue _mainDeletionQueue;

    VmaAllocator _allocator;

    GPUMeshBuffers rectangle;

        float renderScale = 1.0f;

    std::vector<std::shared_ptr<MeshAsset>> testMeshes;

    bool resize_requested = false;


    DrawContext mainDrawContext;
    std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;

    Camera mainCamera;
};
