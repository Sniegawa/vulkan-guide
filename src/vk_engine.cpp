//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>
#include <vk_images.h>

#include <VkBootstrap.h>

#include <chrono>
#include <thread>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtc/matrix_transform.hpp>


VulkanEngine* loadedEngine = nullptr;

constexpr bool bUseValidationLayers = true;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }

void VulkanEngine::init()
{
	// only one engine initialization is allowed with the application.
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	// We initialize SDL and create a window with it.
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags);

	init_vulkan();

	init_swapchain();

	init_commands();
	
	init_sync_structures();

	init_descriptors();

	init_pipelines();

	init_default_data();

	init_imgui();

	// everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_default_data()
{
	testMeshes = loadGltfMeshes(this, "..\\assets\\basicmesh.glb").value();
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	// Create instance using VkBoostrap
	auto inst_ret = builder.set_app_name("Vulkan-guide application")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	// Collect the instance handles
	_instance = vkb_inst.instance;
	m_mainDeletionQueue.push_function([&]() {
		vkDestroyInstance(_instance, nullptr);
	});

	_debug_messenger = vkb_inst.debug_messenger;
	m_mainDeletionQueue.push_function([&]() {
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	});

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);



	VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = true; // GPU pointer without bindings
	features12.descriptorIndexing = true; // bindless textures

	// Let VkBootstrap select a gpu
	// The requirements are
	// - Be able to draw to SDL surface
	// - supports above features

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

	
	// It creates the final vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	m_mainDeletionQueue.push_function([&]() {
		vkDestroyDevice(_device, nullptr);
	});

	_chosenGPU = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value(); // Queue for graphics - queue with (almost)every command


	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; // This is to let us use gpu pointers
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	m_mainDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(_allocator);
	});
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	_swapchainData.swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainData.swapchainImageFormat,.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) //VK_PRESENT_MODE_FIFO_KHR | VK_PRESENT_MODE_IMMEDIATE_KHR
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainData.swapchainExtent = vkbSwapchain.extent;

	_swapchainData.swapchain = vkbSwapchain.swapchain;
	_swapchainData.swapchainImages = vkbSwapchain.get_images().value();
	_swapchainData.swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::init_swapchain()
{
	create_swapchain(_windowExtent.width, _windowExtent.height);

	VkExtent3D drawImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);
	vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));

	m_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
	});
}

void VulkanEngine::init_commands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i].commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));
	}

	//----------------------------\\
	// Immediate mode command pool\\
	//----------------------------\\
	
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

	m_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);
	});
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmdBfr)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmdBfr = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBfrBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmdBfr, &cmdBfrBeginInfo));

	function(cmdBfr);

	VK_CHECK(vkEndCommandBuffer(cmdBfr));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::command_buffer_submit_info(cmdBfr);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void VulkanEngine::init_sync_structures()
{
	// One fence to controls when the gpu has finished rendering the frame
	// and 2 semaphores to synchronize rendering with swapchain

	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT); // That flags means we can wait on created fence
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for(int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));
	}

	// Create swapchain image available semaphores
	
	uint32_t imageCount = _swapchainData.swapchainImages.size();
	
	_swapchainData.imageAvailableSemaphores.resize(imageCount + 1);
	_swapchainData.imageOwnerSemaphores.resize(imageCount, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo createInfo = vkinit::semaphore_create_info();
	for (auto& sem : _swapchainData.imageAvailableSemaphores)
	{
		VK_CHECK(vkCreateSemaphore(_device, &createInfo, nullptr, &sem));
		_swapchainData.freeSemaphores.push_back(sem);
	}

	_swapchainData.renderSemaphores.resize(imageCount);
	for (auto& sem : _swapchainData.renderSemaphores)
		VK_CHECK(vkCreateSemaphore(_device, &createInfo, nullptr, &sem));

	_swapchainData._dQueue.push_function([&]() {
		vkDestroySwapchainKHR(_device, _swapchainData.swapchain, nullptr);

		for (int i = 0; i < _swapchainData.swapchainImageViews.size(); i++)
		{
			vkDestroyImageView(_device, _swapchainData.swapchainImageViews[i], nullptr);
		}
		for (auto& sem : _swapchainData.imageAvailableSemaphores)
			vkDestroySemaphore(_device, sem, nullptr);

		for (auto& sem : _swapchainData.renderSemaphores)
			vkDestroySemaphore(_device, sem, nullptr);
		});



	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	m_mainDeletionQueue.push_function([=]() { vkDestroyFence(_device, _immFence, nullptr); });
}

void VulkanEngine::init_descriptors()
{
	std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
	};

	globalDescriptorAllocator.init_pool(_device, 10, sizes);

	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	_drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = _drawImage.imageView;

	VkWriteDescriptorSet drawImageWrite = {};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;

	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = _drawImageDescriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

	m_mainDeletionQueue.push_function([&]() {
		globalDescriptorAllocator.destroy_pool(_device);

		vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
	});
}

void VulkanEngine::init_pipelines()
{
	init_background_pipelines();
	init_mesh_pipeline();
}

void VulkanEngine::init_background_pipelines()
{
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant = {};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!vkutil::load_shader_module("../shaders/gradient_color.comp.spv", _device, &gradientShader))
		fmt::print("Error when building the compute shader \n");

	VkShaderModule skyShader;
	if (!vkutil::load_shader_module("../shaders/sky.comp.spv", _device, &skyShader))
		fmt::print("Error when building the compute shader \n");


	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _gradientPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};

	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

	// Second shader

	computePipelineCreateInfo.stage.module = skyShader;

	ComputeEffect sky;
	sky.layout = _gradientPipelineLayout;
	sky.name = "sky";
	sky.data = {};

	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);

	vkDestroyShaderModule(_device, gradientShader, nullptr);
	vkDestroyShaderModule(_device, skyShader, nullptr);

	m_mainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, sky.pipeline, nullptr);
		vkDestroyPipeline(_device, gradient.pipeline, nullptr);
	});
}

void VulkanEngine::init_mesh_pipeline()
{
	VkShaderModule triangleVertexShader;
	if (!vkutil::load_shader_module("../shaders/colored_triangle_mesh.vert.spv", _device, &triangleVertexShader))
		fmt::print("Error when building the triangle vertex shader module");
	else
		fmt::print("Triangle vertex shader succesfully loaded");

	VkShaderModule triangleFragShader;
	if (!vkutil::load_shader_module("../shaders/color_triangle.frag.spv", _device, &triangleFragShader))
		fmt::print("Error when building the triangle fragment shader module");
	else
		fmt::print("Triangle fragment shader succesfully loaded");


	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;


	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
	pipeline_layout_info.pPushConstantRanges = &bufferRange;
	pipeline_layout_info.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

	vkutil::PipelineBuilder pipelineBuilder;

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true,VK_COMPARE_OP_GREATER_OR_EQUAL);

	pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	pipelineBuilder.set_depth_format(_depthImage.imageFormat);

	_meshPipeline = pipelineBuilder.build_pipeline(_device);


	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);


	m_mainDeletionQueue.push_function([=]() {
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
	});
}

void VulkanEngine::init_imgui()
{
	// Copied from the imgui demo
	VkDescriptorPoolSize pool_sizes[] = {
		{VK_DESCRIPTOR_TYPE_SAMPLER,1000},
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	// actual imgui initialization

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	// dynamic rendering parameters
	auto& PipelineCreateInfo = init_info.PipelineRenderingCreateInfo;
	PipelineCreateInfo = {};
	PipelineCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	PipelineCreateInfo.colorAttachmentCount = 1;
	PipelineCreateInfo.pColorAttachmentFormats = &_swapchainData.swapchainImageFormat;

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();

	m_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
	});

}

void VulkanEngine::draw_background(VkCommandBuffer cmdBfr)
{
	//VkClearColorValue clearValue;
	//float flash = std::abs(std::sin(_frameNumber / 120.0f));
	//clearValue = { {0.0f, 0.0f, flash, 1.0f} };

	//VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	//vkCmdClearColorImage(cmdBfr, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];

	vkCmdBindPipeline(cmdBfr, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	vkCmdBindDescriptorSets(cmdBfr, VK_PIPELINE_BIND_POINT_COMPUTE, effect.layout, 0, 1, &_drawImageDescriptors, 0, nullptr);

	vkCmdPushConstants(cmdBfr, effect.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

	vkCmdDispatch(cmdBfr, std::ceil(_drawExtent.width / 16), std::ceil(_drawExtent.height / 16), 1);
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmdBfr)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.0f;
	VkRenderingAttachmentInfo depthAttachment = vkinit::attachment_info(_depthImage.imageView, &depthClear, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmdBfr, &renderInfo);

	vkCmdBindPipeline(cmdBfr, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _drawExtent.width;
	viewport.height = _drawExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmdBfr, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;

	vkCmdSetScissor(cmdBfr, 0, 1, &scissor);

	GPUDrawPushConstants push_constants;
	push_constants.worldMatrix = glm::mat4(1.0f);

	push_constants.vertexBuffer = testMeshes[currentMesh]->meshBuffers.vertexBufferAddress;

	glm::mat4 view = glm::translate(glm::mat4(1.0f),glm::vec3(0.0,0.0,-5));

	glm::mat4 projection = glm::perspective(glm::radians(70.0f), (float)_drawExtent.width / (float)_drawExtent.height, 10000.0f, 0.1f);

	projection[1][1] *= -1;

	push_constants.worldMatrix = projection * view;

	vkCmdPushConstants(cmdBfr, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
	vkCmdBindIndexBuffer(cmdBfr,testMeshes[currentMesh]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmdBfr, testMeshes[currentMesh]->surfaces[0].count, 1, testMeshes[currentMesh]->surfaces[0].startIndex, 0, 0);

	vkCmdEndRendering(cmdBfr);
}

void VulkanEngine::draw()
{
	//-----------------------------------------------------\\
	// Wait for (frame - FRAMES IN FLIGHT) to be completed \\
	//-----------------------------------------------------\\

	auto& CurrentFrame = get_current_frame();
	VK_CHECK(vkWaitForFences(_device, 1, &CurrentFrame.renderFence, true, 1000000000)); // Timeout is 1000000000ns = 1s
	VK_CHECK(vkResetFences(_device, 1, &CurrentFrame.renderFence));

	//---------------------\\
	// Get swapchain image \\
	//---------------------\\

	assert(!_swapchainData.freeSemaphores.empty());
	VkSemaphore acquireSemaphore = _swapchainData.freeSemaphores.back();
	_swapchainData.freeSemaphores.pop_back();

	uint32_t swapchainImageIndex;

	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchainData.swapchain, 1000000000, acquireSemaphore, nullptr, &swapchainImageIndex));

	// If this imageIndex has its owning semaphore recycle it
	if (_swapchainData.imageOwnerSemaphores[swapchainImageIndex] != VK_NULL_HANDLE)
		_swapchainData.freeSemaphores.push_back(_swapchainData.imageOwnerSemaphores[swapchainImageIndex]);


	_swapchainData.imageOwnerSemaphores[swapchainImageIndex] = acquireSemaphore;

	VkSemaphore renderSem = _swapchainData.renderSemaphores[swapchainImageIndex];

	//--------------------------------\\
	// Create and fill command buffer \\
	//--------------------------------\\

	VkCommandBuffer cmdBfr = CurrentFrame.mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmdBfr, 0));

	VkCommandBufferBeginInfo cmdBfrBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	_drawExtent.width = _drawImage.imageExtent.width;
	_drawExtent.height = _drawImage.imageExtent.height;

	VkImage& currentImage = _swapchainData.swapchainImages[swapchainImageIndex];

	VK_CHECK(vkBeginCommandBuffer(cmdBfr, &cmdBfrBeginInfo));
	{
		// Transition image from don't care to general
		vkutil::transition_image(cmdBfr, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		draw_background(cmdBfr);

		vkutil::transition_image(cmdBfr, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		vkutil::transition_image(cmdBfr, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		draw_geometry(cmdBfr);

		vkutil::transition_image(cmdBfr, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		vkutil::transition_image(cmdBfr, currentImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		vkutil::copy_image_to_image(cmdBfr, _drawImage.image, currentImage, _drawExtent, _swapchainData.swapchainExtent);

		vkutil::transition_image(cmdBfr, currentImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		draw_imgui(cmdBfr, _swapchainData.swapchainImageViews[swapchainImageIndex]);

		vkutil::transition_image(cmdBfr, currentImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	}
	VK_CHECK(vkEndCommandBuffer(cmdBfr));

	//-------------------------------------\\
	// Prepare the submission to the queue \\
	//-------------------------------------\\

	VkCommandBufferSubmitInfo cmdBfrSubmitInfo = vkinit::command_buffer_submit_info(cmdBfr);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, acquireSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, renderSem);

	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdBfrSubmitInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, CurrentFrame.renderFence));

	//----------------------\\
	// Prepare presentation \\
	//----------------------\\

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchainData.swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &renderSem;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmdBfr, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainData.swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmdBfr, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBfr);

	vkCmdEndRendering(cmdBfr);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,&newBuffer.info));

	return newBuffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers VulkanEngine::upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	// We specify that it is an ssbo
	newSurface.vertexBuffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

	// Index buffer has to specify that it is an index buffer
	newSurface.indexBuffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	// Create staging buffer to write to
	AllocatedBuffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	void* data = staging.allocation->GetMappedData();

	memcpy(data, vertices.data(), vertexBufferSize); // Copy vertex buffer to staging buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize); // Copy index buffer to staging at offset

	immediate_submit([&](VkCommandBuffer cmdBfr) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmdBfr, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy); // Copy Vertex buffer from staging

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmdBfr, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy); // Copy index buffer from staging
	});

	destroy_buffer(staging);

	return newSurface;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	// main loop
	while (!bQuit) {
		// Handle events on queue
		while (SDL_PollEvent(&e) != 0) {
			// close the window when user alt-f4s or clicks the X button
			if (e.type == SDL_QUIT)
				bQuit = true;

			if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					stop_rendering = true;
				}
				if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					stop_rendering = false;
				}
			}

			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		// do not draw if we are minimized
		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();

		ImGui::NewFrame();

		if(ImGui::Begin("background"))
		{
			ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];

			ImGui::Text("Selected effect: ", selected.name);

			ImGui::SliderInt("Effect Index", &currentBackgroundEffect, 0, backgroundEffects.size() - 1);

			ImGui::InputFloat4("data1", (float*)&selected.data.data1);
			ImGui::InputFloat4("data2", (float*)&selected.data.data2);
			ImGui::InputFloat4("data3", (float*)&selected.data.data3);
			ImGui::InputFloat4("data4", (float*)&selected.data.data4);
		}
		ImGui::End();

		if(ImGui::Begin("Mesh selector"))
		{
			ImGui::SliderInt("Selected mesh", &currentMesh, 0, testMeshes.size() - 1);

		}
		ImGui::End();


		if(ImGui::Begin("Stats"))
		{
			ImGui::Text("FPS: %.1f", 1.0f / _deltaTime);
			ImGui::Text("Frame time: %.3f ms", _deltaTime * 1000.0f);
		}
		ImGui::End();

		ImGui::Render();


		auto now = std::chrono::high_resolution_clock::now();
		_deltaTime = std::chrono::duration<float>(now - _lastFrameTime).count();
		_lastFrameTime = now;

		draw();
	}
}

void VulkanEngine::cleanup()
{
	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);

			vkDestroyFence(_device, _frames[i].renderFence, nullptr);
			vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);

			_frames[i]._dQueue.flush();
		}

		_swapchainData._dQueue.flush();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		for (auto& mesh : testMeshes) {
			destroy_buffer(mesh->meshBuffers.indexBuffer);
			destroy_buffer(mesh->meshBuffers.vertexBuffer);
		}

		m_mainDeletionQueue.flush();
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}