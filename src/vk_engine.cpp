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

	// everything went fine
	_isInitialized = true;
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
	m_mainDeletionQueue.push_function([this]() {
		vkDestroyInstance(_instance, nullptr);
	});

	_debug_messenger = vkb_inst.debug_messenger;
	m_mainDeletionQueue.push_function([this]() {
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
	m_mainDeletionQueue.push_function([this]() {
		vkDestroyDevice(_device, nullptr);
	});

	_chosenGPU = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value(); // Queue for graphics - queue with (almost)every command

}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	_swapchainData.swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainData.swapchainImageFormat,.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
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

	_swapchainData._dQueue.push_function([this]() {
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
}

void VulkanEngine::draw()
{
	auto& CurrentFrame = get_current_frame();
	VK_CHECK(vkWaitForFences(_device, 1, &CurrentFrame.renderFence, true, 1000000000)); // Timeout is 1000000000ns = 1s
	VK_CHECK(vkResetFences(_device, 1, &CurrentFrame.renderFence));

	//--------------------\\
	// Get swapchain image\\
	//--------------------\\

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

	//-------------------------------\\
	// Create and fill command buffer\\
	//-------------------------------\\

	VkCommandBuffer cmdBfr = CurrentFrame.mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmdBfr, 0));

	VkCommandBufferBeginInfo cmdBfrBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmdBfr, &cmdBfrBeginInfo));
	{
		// Transition image from don't care to general
		vkutil::transition_image(cmdBfr, _swapchainData.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		VkClearColorValue clearValue;
		float flash = std::abs(std::sin(_frameNumber / 120.0f));
		clearValue = { {0.0f, 0.0f, flash, 1.0f} };

		VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

		vkCmdClearColorImage(cmdBfr, _swapchainData.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

		// Transition image into presentation mode
		vkutil::transition_image(cmdBfr, _swapchainData.swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	}
	VK_CHECK(vkEndCommandBuffer(cmdBfr));

	//------------------------------------\\
	// Prepare the submission to the queue\\
	//------------------------------------\\

	VkCommandBufferSubmitInfo cmdBfrSubmitInfo = vkinit::command_buffer_submit_info(cmdBfr);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, acquireSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, renderSem);

	VkSubmitInfo2 submitInfo = vkinit::submit_info(&cmdBfrSubmitInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, CurrentFrame.renderFence));

	//---------------------\\
	// Prepare presentation\\
	//---------------------\\

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
		}

		// do not draw if we are minimized
		if (stop_rendering) {
			// throttle the speed to avoid the endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

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

		m_mainDeletionQueue.flush();
		SDL_DestroyWindow(_window);
	}

	// clear engine pointer
	loadedEngine = nullptr;
}