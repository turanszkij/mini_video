#include "include/common.h"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif // _WIN32

// The vulkan headers are included in the repository, but the prototypes are defined out to not try to statically link them
#define VK_NO_PROTOTYPES
#include "include/vulkan/vulkan.h"

// The volk library is used to dynamically load Vulkan DLL so we don't have to statically link with Vulkan SDK
#define VOLK_IMPLEMENTATION
#include "include/volk.h"

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Loading test.mp4 because file name was not provided with a startup argument\n");
	}

	Video video;
	if (!video.Load_mp4(argc > 1 ? argv[argc - 1] : "test.mp4"))
	{
		printf("Video load failure, exiting.\n");
		return -1;
	}

	if (video.frame_infos.empty())
	{
		printf("Video was loaded, but there are no frames, exiting.\n");
		return -1;
	}

	printf("Video was loaded successfully and contains %d frames in total.\n", (int)video.frame_infos.size());

	// Below this will be all the Vulkan code:
	VkResult res;
	res = volkInitialize();
	assert(res == VK_SUCCESS);

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "mini_video_vulkan";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "mini_video_vulkan";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// Enumerate available layers and extensions:
	uint32_t instanceLayerCount;
	res = vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
	assert(res == VK_SUCCESS);
	std::vector<VkLayerProperties> availableInstanceLayers(instanceLayerCount);
	res = vkEnumerateInstanceLayerProperties(&instanceLayerCount, availableInstanceLayers.data());
	assert(res == VK_SUCCESS);

	uint32_t extensionCount = 0;
	res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	assert(res == VK_SUCCESS);
	std::vector<VkExtensionProperties> availableInstanceExtensions(extensionCount);
	res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableInstanceExtensions.data());
	assert(res == VK_SUCCESS);

	std::vector<const char*> instanceLayers;
	std::vector<const char*> instanceExtensions;

	bool debugUtils = false;
#ifdef _DEBUG
	for (auto& availableExtension : availableInstanceExtensions)
	{
		if (strcmp(availableExtension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
		{
			debugUtils = true;
			instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
		}
	}
#endif // _DEBUG

	instanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
	instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif // _WIN32

	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugUtilsMessenger = VK_NULL_HANDLE;

	// Create instance:
	{
		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
		createInfo.ppEnabledLayerNames = instanceLayers.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
		createInfo.ppEnabledExtensionNames = instanceExtensions.data();

		VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo = {};
		debugUtilsCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		if (debugUtils)
		{
			debugUtilsCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			debugUtilsCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

			static auto debugUtilsMessengerCallback = [](
				VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
				VkDebugUtilsMessageTypeFlagsEXT message_type,
				const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
				void* user_data) -> VkBool32
				{
					if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
					{
						printf("[Vulkan Warning]: %s\n", callback_data->pMessage);
					}
					else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
					{
						printf("[Vulkan Error]: %s\n", callback_data->pMessage);
					}
					assert(0);
					return VK_FALSE;
				};
			debugUtilsCreateInfo.pfnUserCallback = debugUtilsMessengerCallback;
			createInfo.pNext = &debugUtilsCreateInfo;
		}

		res = vkCreateInstance(&createInfo, nullptr, &instance);
		assert(res == VK_SUCCESS);

		volkLoadInstanceOnly(instance);

		if (debugUtils)
		{
			res = vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsCreateInfo, nullptr, &debugUtilsMessenger);
			assert(res == VK_SUCCESS);
		}
	}

	// Enumerating and creating devices:
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t videoFamily = VK_QUEUE_FAMILY_IGNORED;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue videoQueue = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties device_memory_properties = {};
	VkVideoDecodeH264ProfileInfoKHR decode_h264_profile = {};
	VkVideoDecodeH264CapabilitiesKHR decode_h264_capabilities = {};
	struct VideoCapability
	{
		VkVideoProfileInfoKHR profile = {};
		VkVideoDecodeCapabilitiesKHR decode_capabilities = {};
		VkVideoCapabilitiesKHR video_capabilities = {};
	};
	VideoCapability video_capability_h264 = {};
	{
		uint32_t deviceCount = 0;
		res = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		assert(res == VK_SUCCESS);
		if (deviceCount == 0)
		{
			printf("Failed to find GPU with Vulkan 1.3 support!");
			return -2;
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		res = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		assert(res == VK_SUCCESS);

		std::vector<const char*> enabled_deviceExtensions;

		for (VkPhysicalDevice dev : devices)
		{
			uint32_t extensionCount = 0;
			res = vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);
			assert(res == VK_SUCCESS);
			std::vector<VkExtensionProperties> available_deviceExtensions(extensionCount);
			res = vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, available_deviceExtensions.data());
			assert(res == VK_SUCCESS);

			auto checkExtensionSupport = [&](const char* checkExtension, const std::vector<VkExtensionProperties>& available_extensions) {
				for (const auto& x : available_extensions)
				{
					if (strcmp(x.extensionName, checkExtension) == 0)
					{
						return true;
					}
				}
				return false;
			};
			if (checkExtensionSupport(VK_KHR_SWAPCHAIN_EXTENSION_NAME, available_deviceExtensions) &&
				checkExtensionSupport(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME, available_deviceExtensions) &&
				checkExtensionSupport(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME, available_deviceExtensions) &&
				checkExtensionSupport(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME, available_deviceExtensions))
			{
				enabled_deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
				enabled_deviceExtensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
				enabled_deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
				enabled_deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
				physicalDevice = dev; // for now accept the first device that supports swapchain and H264 decode
				break;
			}
		}

		if (physicalDevice == VK_NULL_HANDLE)
		{
			printf("Failed to find a GPU that supports Vulkan H264 decoding!");
			return -3;
		}

		decode_h264_profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
		decode_h264_profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
		decode_h264_profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_INTERLEAVED_LINES_BIT_KHR;

		decode_h264_capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;

		video_capability_h264.profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
		video_capability_h264.profile.pNext = &decode_h264_profile;
		video_capability_h264.profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
		video_capability_h264.profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		video_capability_h264.profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		video_capability_h264.profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;

		video_capability_h264.decode_capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;

		video_capability_h264.video_capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
		video_capability_h264.video_capabilities.pNext = &video_capability_h264.decode_capabilities;
		video_capability_h264.decode_capabilities.pNext = &decode_h264_capabilities;
		res = vkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &video_capability_h264.profile, &video_capability_h264.video_capabilities);
		assert(res == VK_SUCCESS);

		// Find queue families:
		std::vector<VkQueueFamilyProperties2> queueFamilies;
		std::vector<VkQueueFamilyVideoPropertiesKHR> queueFamiliesVideo;
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, nullptr);

		queueFamilies.resize(queueFamilyCount);
		queueFamiliesVideo.resize(queueFamilyCount);
		for (uint32_t i = 0; i < queueFamilyCount; ++i)
		{
			queueFamilies[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
			queueFamilies[i].pNext = &queueFamiliesVideo[i];
			queueFamiliesVideo[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
		}
		vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, queueFamilies.data());

		for (uint32_t i = 0; i < queueFamilyCount; ++i)
		{
			auto& queueFamily = queueFamilies[i];
			auto& queueFamilyVideo = queueFamiliesVideo[i];

			if (graphicsFamily == VK_QUEUE_FAMILY_IGNORED && 
				queueFamily.queueFamilyProperties.queueCount > 0 && 
				queueFamily.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT
				)
			{
				graphicsFamily = i;
			}

			if (videoFamily == VK_QUEUE_FAMILY_IGNORED &&
				queueFamily.queueFamilyProperties.queueCount > 0 &&
				(queueFamily.queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) &&
				(queueFamilyVideo.videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
				)
			{
				videoFamily = i;
			}
		}

		if (videoFamily == VK_QUEUE_FAMILY_IGNORED)
		{
			printf("Failed to find queue with VK_QUEUE_VIDEO_DECODE_BIT_KHR and VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR support!");
			return -4;
		}

		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueCreateInfos[2] = {};
		{
			queueCreateInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[0].queueFamilyIndex = graphicsFamily;
			queueCreateInfos[0].queueCount = 1;
			queueCreateInfos[0].pQueuePriorities = &queuePriority;
		}
		{
			queueCreateInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfos[1].queueFamilyIndex = videoFamily;
			queueCreateInfos[1].queueCount = 1;
			queueCreateInfos[1].pQueuePriorities = &queuePriority;
		}

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = arraysize(queueCreateInfos);
		createInfo.pQueueCreateInfos = queueCreateInfos;
		createInfo.pEnabledFeatures = nullptr;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabled_deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = enabled_deviceExtensions.data();

		res = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
		if (res != VK_SUCCESS)
		{
			printf("vkCreateDevice error: %d", (int)res);
			return -5;
		}

		volkLoadDevice(device);

		vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, videoFamily, 0, &videoQueue);

		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &device_memory_properties);
	}

	// Debug object name helper:
	static auto set_name = [&](uint64_t handle, VkObjectType type, const char* name) {
		if (!debugUtils)
			return;
		VkDebugUtilsObjectNameInfoEXT name_info = {};
		name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		name_info.objectType = type;
		name_info.pObjectName = name;
		name_info.objectHandle = handle;
		res = vkSetDebugUtilsObjectNameEXT(device, &name_info);
		assert(res == VK_SUCCESS);
	};

	// Create bitstream GPU buffer and copy compressed video data into it:
	VkBuffer bitstream_buffer = VK_NULL_HANDLE;
	VkDeviceMemory bitstream_buffer_memory = VK_NULL_HANDLE;
	const VkDeviceSize bitstream_alignment = align(video_capability_h264.video_capabilities.minBitstreamBufferOffsetAlignment, video_capability_h264.video_capabilities.minBitstreamBufferSizeAlignment);
	{
		// The bitstream offsets are at this point pointing into the H264 data that was loaded, but we need to change it to point to aligned GPU data:
		uint64_t aligned_size = 0;
		for (Video::FrameInfo& frame_info : video.frame_infos)
		{
			aligned_size += align(frame_info.size, bitstream_alignment);
		}

		VkBufferCreateInfo buffer_info = {};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.size = aligned_size;
		buffer_info.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
		VkVideoProfileListInfoKHR profile_list_info = {};
		profile_list_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
		profile_list_info.pProfiles = &video_capability_h264.profile;
		profile_list_info.profileCount = 1;
		buffer_info.pNext = &profile_list_info;
		res = vkCreateBuffer(device, &buffer_info, nullptr, &bitstream_buffer);
		assert(res == VK_SUCCESS);

		VkMemoryRequirements buffer_memory_requirements = {};
		vkGetBufferMemoryRequirements(device, bitstream_buffer, &buffer_memory_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = buffer_memory_requirements.size;
		// Search for mappable (host visible) memory type in the device_memory_properties that's appropriate for buffer_memory_requirements.memoryTypeBits
		//	Note: Nvidia GPU had problems with non-mappable memory type last time I checked
		while (buffer_memory_requirements.memoryTypeBits != 0 && (device_memory_properties.memoryTypes[firstbitlow(buffer_memory_requirements.memoryTypeBits)].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
		{
			buffer_memory_requirements.memoryTypeBits ^= 1u << firstbitlow(buffer_memory_requirements.memoryTypeBits); // remove unmappable bit from buffer_memory_requirements.memoryTypeBits
		}
		alloc_info.memoryTypeIndex = firstbitlow(buffer_memory_requirements.memoryTypeBits);
		res = vkAllocateMemory(device, &alloc_info, nullptr, &bitstream_buffer_memory);
		assert(res == VK_SUCCESS);

		res = vkBindBufferMemory(device, bitstream_buffer, bitstream_buffer_memory, 0);
		assert(res == VK_SUCCESS);

		void* mapped_data = nullptr;
		res = vkMapMemory(device, bitstream_buffer_memory, 0, buffer_info.size, 0, &mapped_data);
		assert(res == VK_SUCCESS);

		// Write the slice datas into the aligned offsets, and store the aligned offsets in frame_infos, from here they will be storing offsets into the bitstream buffer, and not the source file:
		uint64_t aligned_offset = 0;
		for (Video::FrameInfo& frame_info : video.frame_infos)
		{
			std::memcpy((uint8_t*)mapped_data + aligned_offset, video.h264_data.data() + frame_info.offset, frame_info.size); // copy into GPU buffer through mapped_data ptr
			frame_info.offset = aligned_offset;
			aligned_offset += align(frame_info.size, bitstream_alignment);
		}

		// The h264_data is not required any longer to be in RAM because it has been copied to the GPU buffer, so I delete it:
		video.h264_data.clear();

		vkUnmapMemory(device, bitstream_buffer_memory);
	}

	// This sample implementation only supports the following texture format when decoding. It has two planes: Luminance and Chrominance. It can be combined to an RGB image by a shader by sampling from both planes.
	const VkFormat decode_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM; // It matches the layout of DXGI_FORMAT_NV12

	// At least one of these two must be supported:
	//	It seems Nvidia is coincide, AMD is distinct at least on what I tested, so it's a good idea to support coincide and no-coincide modes
	//	- in coincide mode, the DPB can be used as shader resource or for transfers
	//	- in distinct mode we will use one more texture that's not DPB but used as video decode destination
	//	- in both coincide and distinct modes, I also retain a reordered picture array, so the DPB is not used directly by a shader, although it's possible
	//		for example you could keep the reordered picture buffer in RGB resolved format, and resolve into it directly from DPB or decode output
	//		but in this example I resolve directly to the swap chain instead, and keep the reordered pictures in YUV format
	const bool dpb_output_coincide_supported = video_capability_h264.decode_capabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
	const bool dpb_output_distinct_supported = video_capability_h264.decode_capabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;

	// Create DPB texture:
	VkImage dpb_image = VK_NULL_HANDLE;
	VkDeviceMemory dpb_image_memory = VK_NULL_HANDLE;
	VkImageView dpb_image_view = VK_NULL_HANDLE;
	VkImageLayout dpb_layouts[17] = {};
	{
		VkImageCreateInfo image_info = {};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = decode_format;
		image_info.extent.width = video.padded_width;
		image_info.extent.height = video.padded_height;
		image_info.extent.depth = 1;
		image_info.arrayLayers = video.num_dpb_slots;
		image_info.mipLevels = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
		if (dpb_output_coincide_supported)
		{
			image_info.usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
			image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		VkVideoProfileListInfoKHR profile_list_info = {};
		profile_list_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
		profile_list_info.pProfiles = &video_capability_h264.profile;
		profile_list_info.profileCount = 1;
		image_info.pNext = &profile_list_info;
		res = vkCreateImage(device, &image_info, nullptr, &dpb_image);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)dpb_image, VK_OBJECT_TYPE_IMAGE, "dpb_image");

		VkMemoryRequirements image_memory_requirements = {};
		vkGetImageMemoryRequirements(device, dpb_image, &image_memory_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = image_memory_requirements.size;
		alloc_info.memoryTypeIndex = firstbitlow(image_memory_requirements.memoryTypeBits);
		res = vkAllocateMemory(device, &alloc_info, nullptr, &dpb_image_memory);
		assert(res == VK_SUCCESS);

		res = vkBindImageMemory(device, dpb_image, dpb_image_memory, 0);
		assert(res == VK_SUCCESS);

		VkImageViewCreateInfo view_desc = {};
		view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_desc.flags = 0;
		view_desc.image = dpb_image;
		view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_desc.subresourceRange.baseArrayLayer = 0;
		view_desc.subresourceRange.layerCount = image_info.arrayLayers;
		view_desc.subresourceRange.baseMipLevel = 0;
		view_desc.subresourceRange.levelCount = 1;
		view_desc.format = image_info.format;
		view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // Note: the shader expects Texture2DArray even if only 1 layer

		VkImageViewUsageCreateInfo viewUsageInfo = {};
		viewUsageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
		viewUsageInfo.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
		if (dpb_output_coincide_supported)
		{
			viewUsageInfo.usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
		}
		view_desc.pNext = &viewUsageInfo;
		res = vkCreateImageView(device, &view_desc, nullptr, &dpb_image_view);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)dpb_image_view, VK_OBJECT_TYPE_IMAGE_VIEW, "dpb_image_view");
	}

	// Create separate decode output if VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR is not supported:
	VkImage decode_output_image = VK_NULL_HANDLE;
	VkDeviceMemory decode_output_image_memory = VK_NULL_HANDLE;
	VkImageView decode_output_image_view = VK_NULL_HANDLE;
	if (!dpb_output_coincide_supported)
	{
		VkImageCreateInfo image_info = {};
		image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_info.imageType = VK_IMAGE_TYPE_2D;
		image_info.format = decode_format;
		image_info.extent.width = video.padded_width;
		image_info.extent.height = video.padded_height;
		image_info.extent.depth = 1;
		image_info.arrayLayers = 1;
		image_info.mipLevels = 1;
		image_info.samples = VK_SAMPLE_COUNT_1_BIT;
		image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_info.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		VkVideoProfileListInfoKHR profile_list_info = {};
		profile_list_info.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
		profile_list_info.pProfiles = &video_capability_h264.profile;
		profile_list_info.profileCount = 1;
		image_info.pNext = &profile_list_info;
		res = vkCreateImage(device, &image_info, nullptr, &decode_output_image);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)decode_output_image, VK_OBJECT_TYPE_IMAGE, "decode_output_image");

		VkMemoryRequirements image_memory_requirements = {};
		vkGetImageMemoryRequirements(device, decode_output_image, &image_memory_requirements);

		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = image_memory_requirements.size;
		alloc_info.memoryTypeIndex = firstbitlow(image_memory_requirements.memoryTypeBits);
		res = vkAllocateMemory(device, &alloc_info, nullptr, &decode_output_image_memory);
		assert(res == VK_SUCCESS);

		res = vkBindImageMemory(device, decode_output_image, decode_output_image_memory, 0);
		assert(res == VK_SUCCESS);

		VkImageViewCreateInfo view_desc = {};
		view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_desc.flags = 0;
		view_desc.image = decode_output_image;
		view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_desc.subresourceRange.baseArrayLayer = 0;
		view_desc.subresourceRange.layerCount = 1;
		view_desc.subresourceRange.baseMipLevel = 0;
		view_desc.subresourceRange.levelCount = 1;
		view_desc.format = image_info.format;
		view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;

		VkImageViewUsageCreateInfo viewUsageInfo = {};
		viewUsageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
		viewUsageInfo.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
		view_desc.pNext = &viewUsageInfo;
		res = vkCreateImageView(device, &view_desc, nullptr, &decode_output_image_view);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)decode_output_image_view, VK_OBJECT_TYPE_IMAGE_VIEW, "decode_output_image_view");
	}

	struct DecodeResultReordered
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView image_view_luminance = VK_NULL_HANDLE;
		VkImageView image_view_chrominance = VK_NULL_HANDLE;
		int display_order = -1;
		int frame_index = 0;
		void create(VkDevice device, uint32_t padded_width, uint32_t padded_height)
		{
			VkImageCreateInfo image_info = {};
			image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			image_info.imageType = VK_IMAGE_TYPE_2D;
			image_info.format = decode_format;
			image_info.extent.width = padded_width;
			image_info.extent.height = padded_height;
			image_info.extent.depth = 1;
			image_info.arrayLayers = 1;
			image_info.mipLevels = 1;
			image_info.samples = VK_SAMPLE_COUNT_1_BIT;
			image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
			image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			VkResult res = vkCreateImage(device, &image_info, nullptr, &image);
			assert(res == VK_SUCCESS);
			set_name((uint64_t)image, VK_OBJECT_TYPE_IMAGE, "DecodeResultReordered::image");

			VkMemoryRequirements image_memory_requirements = {};
			vkGetImageMemoryRequirements(device, image, &image_memory_requirements);

			VkMemoryAllocateInfo alloc_info = {};
			alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize = image_memory_requirements.size;
			alloc_info.memoryTypeIndex = firstbitlow(image_memory_requirements.memoryTypeBits);
			res = vkAllocateMemory(device, &alloc_info, nullptr, &memory);
			assert(res == VK_SUCCESS);

			res = vkBindImageMemory(device, image, memory, 0);
			assert(res == VK_SUCCESS);

			VkImageViewCreateInfo view_desc = {};
			view_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_desc.flags = 0;
			view_desc.image = image;
			view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_desc.subresourceRange.baseArrayLayer = 0;
			view_desc.subresourceRange.layerCount = 1;
			view_desc.subresourceRange.baseMipLevel = 0;
			view_desc.subresourceRange.levelCount = 1;
			view_desc.format = image_info.format;
			view_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;

			VkImageViewUsageCreateInfo viewUsageInfo = {};
			viewUsageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
			viewUsageInfo.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
			view_desc.pNext = &viewUsageInfo;

			viewUsageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
			view_desc.format = VK_FORMAT_R8_UNORM;
			res = vkCreateImageView(device, &view_desc, nullptr, &image_view_luminance);
			assert(res == VK_SUCCESS);
			set_name((uint64_t)image_view_luminance, VK_OBJECT_TYPE_IMAGE_VIEW, "DecodeResultReordered::image_view_luminance");
			view_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
			view_desc.format = VK_FORMAT_R8G8_UNORM;
			res = vkCreateImageView(device, &view_desc, nullptr, &image_view_chrominance);
			assert(res == VK_SUCCESS);
			set_name((uint64_t)image_view_chrominance, VK_OBJECT_TYPE_IMAGE_VIEW, "DecodeResultReordered::image_view_chrominance");
		}
		void destroy(VkDevice device)
		{
			vkDestroyImageView(device, image_view_luminance, nullptr);
			vkDestroyImageView(device, image_view_chrominance, nullptr);
			vkDestroyImage(device, image, nullptr);
			vkFreeMemory(device, memory, nullptr);
		}
	};
	std::vector<DecodeResultReordered> reordered_results_free; // free image slots that can be reused as reordering image displaying
	std::vector<DecodeResultReordered> reordered_results_working; // image slots that are currently in use for reordering
	DecodeResultReordered displayed_image; // the latest reordered picture

	VkExtent2D codedExtent = {};
	codedExtent.width = std::min(video.padded_width, video_capability_h264.video_capabilities.maxCodedExtent.width);
	codedExtent.height = std::min(video.padded_height, video_capability_h264.video_capabilities.maxCodedExtent.height);

	// Create video decoder:
	VkVideoSessionKHR video_session = VK_NULL_HANDLE;
	std::vector<VkDeviceMemory> video_session_allocations;
	VkVideoSessionParametersKHR session_parameters = VK_NULL_HANDLE;
	{
		std::vector<StdVideoH264PictureParameterSet> pps_array_h264(video.pps_array.size());
		std::vector<StdVideoH264ScalingLists> scalinglist_array_h264(video.pps_array.size());
		for (uint32_t i = 0; i < pps_array_h264.size(); ++i)
		{
			const h264::PPS& pps = video.pps_array[i];
			StdVideoH264PictureParameterSet& vk_pps = pps_array_h264[i];
			StdVideoH264ScalingLists& vk_scalinglist = scalinglist_array_h264[i];

			vk_pps.flags.transform_8x8_mode_flag = pps.transform_8x8_mode_flag;
			vk_pps.flags.redundant_pic_cnt_present_flag = pps.redundant_pic_cnt_present_flag;
			vk_pps.flags.constrained_intra_pred_flag = pps.constrained_intra_pred_flag;
			vk_pps.flags.deblocking_filter_control_present_flag = pps.deblocking_filter_control_present_flag;
			vk_pps.flags.weighted_pred_flag = pps.weighted_pred_flag;
			vk_pps.flags.bottom_field_pic_order_in_frame_present_flag = pps.pic_order_present_flag;
			vk_pps.flags.entropy_coding_mode_flag = pps.entropy_coding_mode_flag;
			vk_pps.flags.pic_scaling_matrix_present_flag = pps.pic_scaling_matrix_present_flag;

			vk_pps.seq_parameter_set_id = pps.seq_parameter_set_id;
			vk_pps.pic_parameter_set_id = pps.pic_parameter_set_id;
			vk_pps.num_ref_idx_l0_default_active_minus1 = pps.num_ref_idx_l0_active_minus1;
			vk_pps.num_ref_idx_l1_default_active_minus1 = pps.num_ref_idx_l1_active_minus1;
			vk_pps.weighted_bipred_idc = (StdVideoH264WeightedBipredIdc)pps.weighted_bipred_idc;
			vk_pps.pic_init_qp_minus26 = pps.pic_init_qp_minus26;
			vk_pps.pic_init_qs_minus26 = pps.pic_init_qs_minus26;
			vk_pps.chroma_qp_index_offset = pps.chroma_qp_index_offset;
			vk_pps.second_chroma_qp_index_offset = pps.second_chroma_qp_index_offset;

			vk_pps.pScalingLists = &vk_scalinglist;
			for (int j = 0; j < arraysize(pps.pic_scaling_list_present_flag); ++j)
			{
				vk_scalinglist.scaling_list_present_mask |= pps.pic_scaling_list_present_flag[j] << j;
			}
			for (int j = 0; j < arraysize(pps.UseDefaultScalingMatrix4x4Flag); ++j)
			{
				vk_scalinglist.use_default_scaling_matrix_mask |= pps.UseDefaultScalingMatrix4x4Flag[j] << j;
			}
			for (int j = 0; j < arraysize(pps.ScalingList4x4); ++j)
			{
				for (int k = 0; k < arraysize(pps.ScalingList4x4[j]); ++k)
				{
					vk_scalinglist.ScalingList4x4[j][k] = (uint8_t)pps.ScalingList4x4[j][k];
				}
			}
			for (int j = 0; j < arraysize(pps.ScalingList8x8); ++j)
			{
				for (int k = 0; k < arraysize(pps.ScalingList8x8[j]); ++k)
				{
					vk_scalinglist.ScalingList8x8[j][k] = (uint8_t)pps.ScalingList8x8[j][k];
				}
			}
		}

		uint32_t num_reference_frames = 0;
		std::vector<StdVideoH264SequenceParameterSet> sps_array_h264(video.sps_array.size());
		std::vector<StdVideoH264SequenceParameterSetVui> vui_array_h264(video.sps_array.size());
		std::vector<StdVideoH264HrdParameters> hrd_array_h264(video.sps_array.size());
		for (uint32_t i = 0; i < sps_array_h264.size(); ++i)
		{
			const h264::SPS& sps = video.sps_array[i];
			StdVideoH264SequenceParameterSet& vk_sps = sps_array_h264[i];

			vk_sps.flags.constraint_set0_flag = sps.constraint_set0_flag;
			vk_sps.flags.constraint_set1_flag = sps.constraint_set1_flag;
			vk_sps.flags.constraint_set2_flag = sps.constraint_set2_flag;
			vk_sps.flags.constraint_set3_flag = sps.constraint_set3_flag;
			vk_sps.flags.constraint_set4_flag = sps.constraint_set4_flag;
			vk_sps.flags.constraint_set5_flag = sps.constraint_set5_flag;
			vk_sps.flags.direct_8x8_inference_flag = sps.direct_8x8_inference_flag;
			vk_sps.flags.mb_adaptive_frame_field_flag = sps.mb_adaptive_frame_field_flag;
			vk_sps.flags.frame_mbs_only_flag = sps.frame_mbs_only_flag;
			vk_sps.flags.delta_pic_order_always_zero_flag = sps.delta_pic_order_always_zero_flag;
			vk_sps.flags.separate_colour_plane_flag = sps.separate_colour_plane_flag;
			vk_sps.flags.gaps_in_frame_num_value_allowed_flag = sps.gaps_in_frame_num_value_allowed_flag;
			vk_sps.flags.qpprime_y_zero_transform_bypass_flag = sps.qpprime_y_zero_transform_bypass_flag;
			vk_sps.flags.frame_cropping_flag = sps.frame_cropping_flag;
			vk_sps.flags.seq_scaling_matrix_present_flag = sps.seq_scaling_matrix_present_flag;
			vk_sps.flags.vui_parameters_present_flag = sps.vui_parameters_present_flag;

			if (vk_sps.flags.vui_parameters_present_flag)
			{
				StdVideoH264SequenceParameterSetVui& vk_vui = vui_array_h264[i];
				vk_sps.pSequenceParameterSetVui = &vk_vui;
				vk_vui.flags.aspect_ratio_info_present_flag = sps.vui.aspect_ratio_info_present_flag;
				vk_vui.flags.overscan_info_present_flag = sps.vui.overscan_info_present_flag;
				vk_vui.flags.overscan_appropriate_flag = sps.vui.overscan_appropriate_flag;
				vk_vui.flags.video_signal_type_present_flag = sps.vui.video_signal_type_present_flag;
				vk_vui.flags.video_full_range_flag = sps.vui.video_full_range_flag;
				vk_vui.flags.color_description_present_flag = sps.vui.colour_description_present_flag;
				vk_vui.flags.chroma_loc_info_present_flag = sps.vui.chroma_loc_info_present_flag;
				vk_vui.flags.timing_info_present_flag = sps.vui.timing_info_present_flag;
				vk_vui.flags.fixed_frame_rate_flag = sps.vui.fixed_frame_rate_flag;
				vk_vui.flags.bitstream_restriction_flag = sps.vui.bitstream_restriction_flag;
				vk_vui.flags.nal_hrd_parameters_present_flag = sps.vui.nal_hrd_parameters_present_flag;
				vk_vui.flags.vcl_hrd_parameters_present_flag = sps.vui.vcl_hrd_parameters_present_flag;

				vk_vui.aspect_ratio_idc = (StdVideoH264AspectRatioIdc)sps.vui.aspect_ratio_idc;
				vk_vui.sar_width = sps.vui.sar_width;
				vk_vui.sar_height = sps.vui.sar_height;
				vk_vui.video_format = sps.vui.video_format;
				vk_vui.colour_primaries = sps.vui.colour_primaries;
				vk_vui.transfer_characteristics = sps.vui.transfer_characteristics;
				vk_vui.matrix_coefficients = sps.vui.matrix_coefficients;
				vk_vui.num_units_in_tick = sps.vui.num_units_in_tick;
				vk_vui.time_scale = sps.vui.time_scale;
				vk_vui.max_num_reorder_frames = sps.vui.num_reorder_frames;
				vk_vui.max_dec_frame_buffering = sps.vui.max_dec_frame_buffering;
				vk_vui.chroma_sample_loc_type_top_field = sps.vui.chroma_sample_loc_type_top_field;
				vk_vui.chroma_sample_loc_type_bottom_field = sps.vui.chroma_sample_loc_type_bottom_field;

				StdVideoH264HrdParameters& vk_hrd = hrd_array_h264[i];
				vk_vui.pHrdParameters = &vk_hrd;
				vk_hrd.cpb_cnt_minus1 = sps.hrd.cpb_cnt_minus1;
				vk_hrd.bit_rate_scale = sps.hrd.bit_rate_scale;
				vk_hrd.cpb_size_scale = sps.hrd.cpb_size_scale;
				for (int j = 0; j < arraysize(sps.hrd.bit_rate_value_minus1); ++j)
				{
					vk_hrd.bit_rate_value_minus1[j] = sps.hrd.bit_rate_value_minus1[j];
					vk_hrd.cpb_size_value_minus1[j] = sps.hrd.cpb_size_value_minus1[j];
					vk_hrd.cbr_flag[j] = sps.hrd.cbr_flag[j];
				}
				vk_hrd.initial_cpb_removal_delay_length_minus1 = sps.hrd.initial_cpb_removal_delay_length_minus1;
				vk_hrd.cpb_removal_delay_length_minus1 = sps.hrd.cpb_removal_delay_length_minus1;
				vk_hrd.dpb_output_delay_length_minus1 = sps.hrd.dpb_output_delay_length_minus1;
				vk_hrd.time_offset_length = sps.hrd.time_offset_length;
			}

			vk_sps.profile_idc = (StdVideoH264ProfileIdc)sps.profile_idc;
			switch (sps.level_idc)
			{
			case 0:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_0;
				break;
			case 11:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_1;
				break;
			case 12:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_2;
				break;
			case 13:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_3;
				break;
			case 20:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_0;
				break;
			case 21:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_1;
				break;
			case 22:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_2;
				break;
			case 30:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_0;
				break;
			case 31:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_1;
				break;
			case 32:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_2;
				break;
			case 40:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_0;
				break;
			case 41:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_1;
				break;
			case 42:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_2;
				break;
			case 50:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_0;
				break;
			case 51:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_1;
				break;
			case 52:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_2;
				break;
			case 60:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_0;
				break;
			case 61:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_1;
				break;
			case 62:
				vk_sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_2;
				break;
			default:
				assert(0);
				break;
			}
			assert(vk_sps.level_idc <= decode_h264_capabilities.maxLevelIdc);
			//vk_sps.chroma_format_idc = (StdVideoH264ChromaFormatIdc)sps.chroma_format_idc;
			vk_sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420; // only one we support currently
			vk_sps.seq_parameter_set_id = sps.seq_parameter_set_id;
			vk_sps.bit_depth_luma_minus8 = sps.bit_depth_luma_minus8;
			vk_sps.bit_depth_chroma_minus8 = sps.bit_depth_chroma_minus8;
			vk_sps.log2_max_frame_num_minus4 = sps.log2_max_frame_num_minus4;
			vk_sps.pic_order_cnt_type = (StdVideoH264PocType)sps.pic_order_cnt_type;
			vk_sps.offset_for_non_ref_pic = sps.offset_for_non_ref_pic;
			vk_sps.offset_for_top_to_bottom_field = sps.offset_for_top_to_bottom_field;
			vk_sps.log2_max_pic_order_cnt_lsb_minus4 = sps.log2_max_pic_order_cnt_lsb_minus4;
			vk_sps.num_ref_frames_in_pic_order_cnt_cycle = sps.num_ref_frames_in_pic_order_cnt_cycle;
			vk_sps.max_num_ref_frames = sps.num_ref_frames;
			vk_sps.pic_width_in_mbs_minus1 = sps.pic_width_in_mbs_minus1;
			vk_sps.pic_height_in_map_units_minus1 = sps.pic_height_in_map_units_minus1;
			vk_sps.frame_crop_left_offset = sps.frame_crop_left_offset;
			vk_sps.frame_crop_right_offset = sps.frame_crop_right_offset;
			vk_sps.frame_crop_top_offset = sps.frame_crop_top_offset;
			vk_sps.frame_crop_bottom_offset = sps.frame_crop_bottom_offset;
			vk_sps.pOffsetForRefFrame = sps.offset_for_ref_frame;

			num_reference_frames = std::max(num_reference_frames, (uint32_t)sps.num_ref_frames);
		}

		num_reference_frames = std::min(num_reference_frames, video_capability_h264.video_capabilities.maxActiveReferencePictures);

		VkVideoDecodeH264SessionParametersAddInfoKHR session_parameters_add_info_h264 = {};
		session_parameters_add_info_h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
		session_parameters_add_info_h264.stdPPSCount = (uint32_t)pps_array_h264.size();
		session_parameters_add_info_h264.pStdPPSs = pps_array_h264.data();
		session_parameters_add_info_h264.stdSPSCount = (uint32_t)sps_array_h264.size();
		session_parameters_add_info_h264.pStdSPSs = sps_array_h264.data();

		VkVideoSessionCreateInfoKHR info = {};
		info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
		info.queueFamilyIndex = videoFamily;
		info.maxActiveReferencePictures = num_reference_frames * 2; // *2: top and bottom field counts as two I think: https://vulkan.lunarg.com/doc/view/1.3.239.0/windows/1.3-extensions/vkspec.html#_video_decode_commands
		info.maxDpbSlots = std::min(video.num_dpb_slots, video_capability_h264.video_capabilities.maxDpbSlots);
		info.maxCodedExtent = codedExtent;
		info.pictureFormat = decode_format;
		info.referencePictureFormat = info.pictureFormat;
		info.pVideoProfile = &video_capability_h264.profile;
		info.pStdHeaderVersion = &video_capability_h264.video_capabilities.stdHeaderVersion;

		res = vkCreateVideoSessionKHR(device, &info, nullptr, &video_session);
		assert(res == VK_SUCCESS);

		uint32_t requirement_count = 0;
		res = vkGetVideoSessionMemoryRequirementsKHR(device, video_session, &requirement_count, nullptr);
		assert(res == VK_SUCCESS);

		std::vector<VkVideoSessionMemoryRequirementsKHR> requirements(requirement_count);
		for (auto& x : requirements)
		{
			x.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
		}
		res = vkGetVideoSessionMemoryRequirementsKHR(device, video_session, &requirement_count, requirements.data());
		assert(res == VK_SUCCESS);

		video_session_allocations.resize(requirement_count);
		std::vector<VkBindVideoSessionMemoryInfoKHR> bind_session_memory_infos(requirement_count);
		for (uint32_t i = 0; i < requirement_count; ++i)
		{
			const VkVideoSessionMemoryRequirementsKHR& video_req = requirements[i];

			VkMemoryAllocateInfo alloc_info = {};
			alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			alloc_info.allocationSize = video_req.memoryRequirements.size;
			alloc_info.memoryTypeIndex = firstbitlow(video_req.memoryRequirements.memoryTypeBits);
			res = vkAllocateMemory(device, &alloc_info, nullptr, &video_session_allocations[i]);
			assert(res == VK_SUCCESS);

			VkBindVideoSessionMemoryInfoKHR& bind_info = bind_session_memory_infos[i];
			bind_info.sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
			bind_info.memory = video_session_allocations[i];
			bind_info.memoryBindIndex = video_req.memoryBindIndex;
			bind_info.memoryOffset = 0;
			bind_info.memorySize = alloc_info.allocationSize;
		}
		res = vkBindVideoSessionMemoryKHR(device, video_session, requirement_count, bind_session_memory_infos.data());
		assert(res == VK_SUCCESS);

		VkVideoDecodeH264SessionParametersCreateInfoKHR session_parameters_info_h264 = {};
		session_parameters_info_h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
		session_parameters_info_h264.maxStdPPSCount = (uint32_t)pps_array_h264.size();
		session_parameters_info_h264.maxStdSPSCount = (uint32_t)sps_array_h264.size();
		session_parameters_info_h264.pParametersAddInfo = &session_parameters_add_info_h264;

		VkVideoSessionParametersCreateInfoKHR session_parameters_info = {};
		session_parameters_info.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
		session_parameters_info.videoSession = video_session;
		session_parameters_info.videoSessionParametersTemplate = VK_NULL_HANDLE;
		session_parameters_info.pNext = &session_parameters_info_h264;
		res = vkCreateVideoSessionParametersKHR(device, &session_parameters_info, nullptr, &session_parameters);
		assert(res == VK_SUCCESS);
	}

	// Create compute pipeline that can resolve Luminance + Chrominance image planes to RGB texture:
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkShaderModule shader_module = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkSampler sampler;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	{
		// Compile the shader with dxcompiler.dll:
		HMODULE dxcompiler = LoadLibrary(L"dxcompiler.dll");
		if (dxcompiler != nullptr)
		{
			DxcCreateInstanceProc  DxcCreateInstance = (DxcCreateInstanceProc)GetProcAddress(dxcompiler, "DxcCreateInstance");
			if (DxcCreateInstance != nullptr)
			{
				Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
				Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;

				HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
				assert(SUCCEEDED(hr));
				hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
				assert(SUCCEEDED(hr));

				std::vector<uint8_t> shadersourcedata;
				std::ifstream shadersourcefile("yuv_to_rgbCS.hlsl", std::ios::binary | std::ios::ate);
				if (shadersourcefile.is_open())
				{
					size_t dataSize = (size_t)shadersourcefile.tellg();
					shadersourcefile.seekg((std::streampos)0);
					shadersourcedata.resize(dataSize);
					shadersourcefile.read((char*)shadersourcedata.data(), dataSize);
					shadersourcefile.close();
				}
				if (shadersourcedata.empty())
				{
					printf("Shader file not found!\n");
					return -1;
				}

				DxcBuffer Source;
				Source.Ptr = shadersourcedata.data();
				Source.Size = shadersourcedata.size();
				Source.Encoding = DXC_CP_ACP;

				const wchar_t* args[] = {
					// switch to spirv compiler:
					L"-spirv",

					// shader type:
					L"-T", L"cs_6_0",

					// entry point:
					L"-E", L"main",

					// Add source file name as last parameter. This will be displayed in error messages
					L"yuv_to_rgbCS.hlsl",
				};

				Microsoft::WRL::ComPtr<IDxcResult> pResults;
				hr = dxcCompiler->Compile(
					&Source,				// Source buffer.
					args,					// Array of pointers to arguments.
					arraysize(args),		// Number of arguments.
					nullptr,				// User-provided interface to handle #include directives (optional).
					IID_PPV_ARGS(&pResults)	// Compiler output status, buffer, and errors.
				);
				assert(SUCCEEDED(hr));

				Microsoft::WRL::ComPtr<IDxcBlobUtf8> pErrors = nullptr;
				hr = pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
				assert(SUCCEEDED(hr));
				if (pErrors != nullptr && pErrors->GetStringLength() != 0)
				{
					printf("Shader compile error: %s", pErrors->GetStringPointer());
					return -1;
				}

				HRESULT hrStatus;
				hr = pResults->GetStatus(&hrStatus);
				assert(SUCCEEDED(hr));
				if (FAILED(hrStatus))
				{
					printf("Shader compiler status error: %d", (int)hrStatus);
					return - 1;
				}

				Microsoft::WRL::ComPtr<IDxcBlob> pShader = nullptr;
				hr = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), nullptr);
				assert(SUCCEEDED(hr));
				if (pShader != nullptr)
				{
					VkShaderModuleCreateInfo moduleInfo = {};
					moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
					moduleInfo.codeSize = pShader->GetBufferSize();
					moduleInfo.pCode = (const uint32_t*)pShader->GetBufferPointer();
					res = vkCreateShaderModule(device, &moduleInfo, nullptr, &shader_module);
					assert(res == VK_SUCCESS);
				}
			}
		}

		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		res = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
		assert(res == VK_SUCCESS);

		VkDescriptorSetLayoutBinding bindings[4] = {};

		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		bindings[0].binding = 0;
		bindings[0].descriptorCount = 1;
		bindings[0].pImmutableSamplers = &sampler;

		bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[1].binding = 1;
		bindings[1].descriptorCount = 1;

		bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[2].binding = 2;
		bindings[2].descriptorCount = 1;

		bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[3].binding = 3;
		bindings[3].descriptorCount = 1;

		VkDescriptorSetLayoutCreateInfo descriptorInfo = {};
		descriptorInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorInfo.pBindings = bindings;
		descriptorInfo.bindingCount = arraysize(bindings);
		res = vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptor_set_layout);
		assert(res == VK_SUCCESS);

		VkPushConstantRange push_constants = {};
		push_constants.size = sizeof(uint32_t) * 2;
		push_constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.pSetLayouts = &descriptor_set_layout;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pPushConstantRanges = &push_constants;
		layoutInfo.pushConstantRangeCount = 1;
		res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline_layout);
		assert(res == VK_SUCCESS);

		VkPipelineShaderStageCreateInfo stageInfo = {};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.pName = "main";
		stageInfo.module = shader_module;

		VkComputePipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = pipeline_layout;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineInfo.stage = stageInfo;
		res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
		assert(res == VK_SUCCESS);

		VkDescriptorPoolSize poolsizes[3] = {};

		poolsizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolsizes[0].descriptorCount = 1;

		poolsizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolsizes[1].descriptorCount = 2;

		poolsizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolsizes[2].descriptorCount = 1;

		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = 1;
		poolInfo.pPoolSizes = poolsizes;
		poolInfo.poolSizeCount = arraysize(poolsizes);
		res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptor_pool);
		assert(res == VK_SUCCESS);
	}

	// Create window:
#ifdef _WIN32
	HINSTANCE hInstance = NULL;
	HWND hWnd = NULL;
	{
		static auto WndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
		{
			switch (message)
			{
			case WM_DESTROY:
				PostQuitMessage(0);
				break;
			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			return 0;
		};
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); // this call is not necesary, but the window will respect the Windows display scaling setting this way
		hInstance = GetModuleHandle(NULL);
		WNDCLASSEXW wcex = {};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = NULL;
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = NULL;
		wcex.lpszClassName = L"mini_video_vulkan";
		wcex.hIconSm = NULL;
		RegisterClassExW(&wcex);
		hWnd = CreateWindowW(L"mini_video_vulkan", L"mini_video_vulkan", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, video.width, video.height, nullptr, nullptr, NULL, nullptr);
		ShowWindow(hWnd, SW_SHOWDEFAULT);
	}
#endif // _WIN32

	// Create swap chain for the window to display the video:
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;
	std::vector<VkSemaphore> swapchain_acquire_semaphores;
	uint32_t swapchain_acquire_semaphore_index = 0;
	VkSemaphore swapchain_release_semaphore = VK_NULL_HANDLE;
	uint32_t swapchain_image_index = 0;
	VkExtent2D swapchain_extent = {};
	auto create_swapchain = [&]() { // because the swapchain will be recreated on window resize, I create a resusable lambda function for it

		// The simplest way is to completely destroy and recreate the swapchain when GPU is idle, I won't bother with anything more advanced in this sample
		vkDeviceWaitIdle(device);
		if (swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device, swapchain, nullptr);
			vkDestroySemaphore(device, swapchain_release_semaphore, nullptr);
			for (auto& x : swapchain_acquire_semaphores)
			{
				vkDestroySemaphore(device, x, nullptr);
			}
			for (auto& x : swapchain_image_views)
			{
				vkDestroyImageView(device, x, nullptr);
			}
		}
		swapchain = VK_NULL_HANDLE;
		swapchain_images.clear();
		swapchain_image_views.clear();
		swapchain_acquire_semaphores.clear();
		swapchain_release_semaphore = VK_NULL_HANDLE;
		swapchain_image_index = 0;
		swapchain_extent = {};

		VkSurfaceKHR swapchain_surface = VK_NULL_HANDLE;

#ifdef _WIN32
		VkWin32SurfaceCreateInfoKHR surface_info = {};
		surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surface_info.hwnd = hWnd;
		surface_info.hinstance = hInstance;
		res = vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &swapchain_surface);
		assert(res == VK_SUCCESS);
#endif // _WIN32

		assert(swapchain_surface != VK_NULL_HANDLE);

		VkBool32 presentSupport = false;
		res = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, graphicsFamily, swapchain_surface, &presentSupport);
		assert(res == VK_SUCCESS);
		if (presentSupport == VK_FALSE)
		{
			printf("Swapchain presentation is not supported on graphics queue! This might result in a crash.");
		}

		VkSurfaceCapabilitiesKHR swapchain_capabilities;
		res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, swapchain_surface, &swapchain_capabilities);
		assert(res == VK_SUCCESS);

		uint32_t formatCount;
		res = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, swapchain_surface, &formatCount, nullptr);
		assert(res == VK_SUCCESS);

		std::vector<VkSurfaceFormatKHR> swapchain_formats(formatCount);
		res = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, swapchain_surface, &formatCount, swapchain_formats.data());
		assert(res == VK_SUCCESS);

		VkSurfaceFormatKHR surfaceFormat = swapchain_formats.front();
		for (auto& x : swapchain_formats)
		{
			if (x.format == VK_FORMAT_R8G8B8A8_UNORM)
			{
				// prefer the VK_FORMAT_R8G8B8_UNORM format because the yuv_to_rgbCS.hlsl shader defines [[vk::image_format("rgba8")]]
				//	this is just to shut up vulkan validator which can complain, but in reality it isn't a problem
				surfaceFormat = x;
				break;
			}
		}

		VkSwapchainCreateInfoKHR swapchain_info = {};
		swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchain_info.surface = swapchain_surface;
		swapchain_info.minImageCount = swapchain_capabilities.minImageCount;
		swapchain_info.imageFormat = surfaceFormat.format;
		swapchain_info.imageColorSpace = surfaceFormat.colorSpace;
		swapchain_info.imageExtent.width = std::max(swapchain_capabilities.minImageExtent.width, std::min(swapchain_capabilities.maxImageExtent.width, video.width));
		swapchain_info.imageExtent.height = std::max(swapchain_capabilities.minImageExtent.height, std::min(swapchain_capabilities.maxImageExtent.height, video.height));
		swapchain_info.imageArrayLayers = 1;
		swapchain_info.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchain_info.preTransform = swapchain_capabilities.currentTransform;
		swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // must be supported, good enough
		swapchain_info.clipped = VK_TRUE;
		swapchain_info.oldSwapchain = VK_NULL_HANDLE;
		res = vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain);
		assert(res == VK_SUCCESS);

		swapchain_format = surfaceFormat.format;
		swapchain_extent = swapchain_info.imageExtent;

		uint32_t imageCount = 0;
		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
		assert(res == VK_SUCCESS);
		swapchain_images.resize(imageCount);
		vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchain_images.data());
		assert(res == VK_SUCCESS);

		// Create swap chain storage images:
		swapchain_image_views.resize(swapchain_images.size());
		for (size_t i = 0; i < swapchain_image_views.size(); ++i)
		{
			VkImageViewCreateInfo view_info = {};
			view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			view_info.image = swapchain_images[i];
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.format = swapchain_format;
			view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.layerCount = 1;
			res = vkCreateImageView(device, &view_info, nullptr, &swapchain_image_views[i]);
			assert(res == VK_SUCCESS);
			set_name((uint64_t)swapchain_image_views[i], VK_OBJECT_TYPE_IMAGE_VIEW, "swapchain_image_view");
		}

		VkSemaphoreCreateInfo semaphore_info = {};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		res = vkCreateSemaphore(device, &semaphore_info, nullptr, &swapchain_release_semaphore);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)swapchain_release_semaphore, VK_OBJECT_TYPE_SEMAPHORE, "swapchain_release_semaphore");

		swapchain_acquire_semaphores.resize(swapchain_images.size());
		for (size_t i = 0; i < swapchain_image_views.size(); ++i)
		{
			res = vkCreateSemaphore(device, &semaphore_info, nullptr, &swapchain_acquire_semaphores[i]);
			assert(res == VK_SUCCESS);
			set_name((uint64_t)swapchain_acquire_semaphores[i], VK_OBJECT_TYPE_SEMAPHORE, "swapchain_acquire_semaphores");
		}

		printf("swapchain resized, new size: %d x %d\n", (int)swapchain_extent.width, (int)swapchain_extent.height);
	};
	create_swapchain();

	// Create command buffers:
	VkCommandPool video_command_pool = VK_NULL_HANDLE;
	VkCommandPool graphics_command_pool = VK_NULL_HANDLE;
	VkCommandBuffer video_cmd = VK_NULL_HANDLE;
	VkCommandBuffer graphics_cmd = VK_NULL_HANDLE;
	VkCommandBufferBeginInfo cmd_begin_info = {};
	VkSemaphore video_semaphore = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	std::vector<VkSemaphore> wait_semaphores;
	{
		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = videoFamily;
		res = vkCreateCommandPool(device, &poolInfo, nullptr, &video_command_pool);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)video_command_pool, VK_OBJECT_TYPE_COMMAND_POOL, "video_command_pool");
		poolInfo.queueFamilyIndex = graphicsFamily;
		res = vkCreateCommandPool(device, &poolInfo, nullptr, &graphics_command_pool);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)graphics_command_pool, VK_OBJECT_TYPE_COMMAND_POOL, "graphics_command_pool");

		VkCommandBufferAllocateInfo commandBufferInfo = {};
		commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferInfo.commandBufferCount = 1;
		commandBufferInfo.commandPool = video_command_pool;
		res = vkAllocateCommandBuffers(device, &commandBufferInfo, &video_cmd);
		assert(res == VK_SUCCESS);
		commandBufferInfo.commandPool = graphics_command_pool;
		res = vkAllocateCommandBuffers(device, &commandBufferInfo, &graphics_cmd);
		assert(res == VK_SUCCESS);

		cmd_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		res = vkCreateSemaphore(device, &info, nullptr, &video_semaphore);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)video_semaphore, VK_OBJECT_TYPE_SEMAPHORE, "video_semaphore");

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		res = vkCreateFence(device, &fenceInfo, nullptr, &fence);
		assert(res == VK_SUCCESS);
		set_name((uint64_t)fence, VK_OBJECT_TYPE_FENCE, "fence");
	}

	// Do the display frame loop:
	bool exiting = false;
	while (!exiting)
	{
#ifdef _WIN32
		// Handle window messages like resize, close, etc:
		MSG msg = { 0 };
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			exiting = msg.message == WM_QUIT;
			continue;
		}
#endif // WIN32

		wait_semaphores.clear();

		if (video.frameIndex == 0)
		{
			// At video beginning, reset the state of picture display reordering: 
			for (DecodeResultReordered& used : reordered_results_working)
			{
				used.display_order = -1;
				reordered_results_free.push_back(used);
			}
			reordered_results_working.clear();
		}

		const bool must_decode = reordered_results_working.size() < video.num_dpb_slots;
		if (must_decode)
		{
			// Decoding a new video frame is required:
			const Video::FrameInfo& frame_info = video.frame_infos[video.frameIndex];
			const h264::SliceHeader& slice_header = video.slice_headers[video.frameIndex];
			const h264::PPS& pps = video.pps_array[slice_header.pic_parameter_set_id];
			const h264::SPS& sps = video.sps_array[pps.seq_parameter_set_id];

			if (frame_info.is_intra)
			{
				video.reference_usage.clear();
				video.next_ref = 0;
				video.next_slot = 0;
			}
			video.current_slot = video.next_slot;
			video.poc_status[video.current_slot] = frame_info.poc;
			video.framenum_status[video.current_slot] = slice_header.frame_num;

			res = vkResetCommandPool(device, video_command_pool, 0);
			assert(res == VK_SUCCESS);
			res = vkBeginCommandBuffer(video_cmd, &cmd_begin_info);
			assert(res == VK_SUCCESS);

			if (video.frameIndex == 0)
			{
				// whole DPB is initialized to DPB layout at first frame:
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.image = dpb_image;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
				barrier.srcAccessMask = VK_ACCESS_NONE;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				vkCmdPipelineBarrier(video_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
				for (int i = 0; i < arraysize(dpb_layouts); ++i)
				{
					dpb_layouts[i] = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
				}
			}
			if (dpb_layouts[video.current_slot] != VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR)
			{
				// if current DPB slot is not in DPB layout, transition it now:
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.image = dpb_image;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
				barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = video.current_slot;
				barrier.subresourceRange.layerCount = 1;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				vkCmdPipelineBarrier(video_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
				dpb_layouts[video.current_slot] = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
			}
			if (!dpb_output_coincide_supported)
			{
				// No-coincide output image needs to be in DECODE_DST layout, and it's always overwritten from undefined:
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.image = decode_output_image;
				barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
				barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				vkCmdPipelineBarrier(video_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
			}

			StdVideoDecodeH264PictureInfo std_picture_info_h264 = {};
			std_picture_info_h264.pic_parameter_set_id = slice_header.pic_parameter_set_id;
			std_picture_info_h264.seq_parameter_set_id = pps.seq_parameter_set_id;
			std_picture_info_h264.frame_num = slice_header.frame_num;
			std_picture_info_h264.PicOrderCnt[0] = frame_info.poc;
			std_picture_info_h264.PicOrderCnt[1] = frame_info.poc;
			std_picture_info_h264.idr_pic_id = slice_header.idr_pic_id;
			std_picture_info_h264.flags.is_intra = frame_info.is_intra ? 1 : 0;
			std_picture_info_h264.flags.is_reference = frame_info.reference_priority > 0 ? 1 : 0;
			std_picture_info_h264.flags.IdrPicFlag = (std_picture_info_h264.flags.is_intra && std_picture_info_h264.flags.is_reference) ? 1 : 0;
			std_picture_info_h264.flags.field_pic_flag = slice_header.field_pic_flag;
			std_picture_info_h264.flags.bottom_field_flag = slice_header.bottom_field_flag;
			std_picture_info_h264.flags.complementary_field_pair = 0;

			VkVideoReferenceSlotInfoKHR reference_slot_infos[17] = {};
			VkVideoPictureResourceInfoKHR reference_slot_pictures[17] = {};
			VkVideoDecodeH264DpbSlotInfoKHR dpb_slots_h264[17] = {};
			StdVideoDecodeH264ReferenceInfo reference_infos_h264[17] = {};
			for (uint32_t i = 0; i < video.num_dpb_slots; ++i)
			{
				VkVideoReferenceSlotInfoKHR& slot = reference_slot_infos[i];
				VkVideoPictureResourceInfoKHR& pic = reference_slot_pictures[i];
				VkVideoDecodeH264DpbSlotInfoKHR& dpb = dpb_slots_h264[i];
				StdVideoDecodeH264ReferenceInfo& ref = reference_infos_h264[i];

				slot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
				slot.pPictureResource = &pic;
				slot.slotIndex = i;
				slot.pNext = &dpb;

				pic.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
				pic.codedOffset.x = 0;
				pic.codedOffset.y = 0;
				pic.codedExtent = codedExtent;
				pic.baseArrayLayer = i;
				pic.imageViewBinding = dpb_image_view;

				dpb.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR;
				dpb.pStdReferenceInfo = &ref;

				ref.flags.bottom_field_flag = 0;
				ref.flags.top_field_flag = 0;
				ref.flags.is_non_existing = 0;
				ref.flags.used_for_long_term_reference = 0;
				ref.FrameNum = video.framenum_status[i];
				ref.PicOrderCnt[0] = video.poc_status[i];
				ref.PicOrderCnt[1] = video.poc_status[i];
			}

			VkVideoReferenceSlotInfoKHR reference_slots[17] = {};
			for (size_t i = 0; i < video.reference_usage.size(); ++i)
			{
				uint32_t ref_slot = video.reference_usage[i];
				assert(ref_slot != video.current_slot);
				reference_slots[i] = reference_slot_infos[ref_slot];
			}
			reference_slots[video.reference_usage.size()] = reference_slot_infos[video.current_slot];
			reference_slots[video.reference_usage.size()].slotIndex = -1;

			VkVideoBeginCodingInfoKHR begin_info = {};
			begin_info.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
			begin_info.videoSession = video_session;
			begin_info.videoSessionParameters = session_parameters;
			begin_info.referenceSlotCount = uint32_t(video.reference_usage.size() + 1); // add in the current reconstructed DPB image
			begin_info.pReferenceSlots = begin_info.referenceSlotCount == 0 ? nullptr : reference_slots;
			vkCmdBeginVideoCodingKHR(video_cmd, &begin_info);

			if (video.frameIndex == 0)
			{
				VkVideoCodingControlInfoKHR control_info = {};
				control_info.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
				control_info.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
				vkCmdControlVideoCodingKHR(video_cmd, &control_info);
			}

			VkVideoDecodeInfoKHR decode_info = {};
			decode_info.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR;
			decode_info.srcBuffer = bitstream_buffer;
			decode_info.srcBufferOffset = (VkDeviceSize)frame_info.offset;
			decode_info.srcBufferRange = (VkDeviceSize)align(frame_info.size, bitstream_alignment);
			if (dpb_output_coincide_supported)
			{
				decode_info.dstPictureResource = *reference_slot_infos[video.current_slot].pPictureResource;
			}
			else
			{
				decode_info.dstPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
				decode_info.dstPictureResource.codedOffset.x = 0;
				decode_info.dstPictureResource.codedOffset.y = 0;
				decode_info.dstPictureResource.codedExtent = codedExtent;
				decode_info.dstPictureResource.baseArrayLayer = 0;
				decode_info.dstPictureResource.imageViewBinding = decode_output_image_view;
			}
			decode_info.referenceSlotCount = uint32_t(video.reference_usage.size());
			decode_info.pReferenceSlots = decode_info.referenceSlotCount == 0 ? nullptr : reference_slots;
			decode_info.pSetupReferenceSlot = &reference_slot_infos[video.current_slot];

			uint32_t slice_offset = 0;

			// https://vulkan.lunarg.com/doc/view/1.3.239.0/windows/1.3-extensions/vkspec.html#_h_264_decoding_parameters
			VkVideoDecodeH264PictureInfoKHR picture_info_h264 = {};
			picture_info_h264.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR;
			picture_info_h264.pStdPictureInfo = &std_picture_info_h264;
			picture_info_h264.sliceCount = 1;
			picture_info_h264.pSliceOffsets = &slice_offset;
			decode_info.pNext = &picture_info_h264;

			vkCmdDecodeVideoKHR(video_cmd, &decode_info);

			VkVideoEndCodingInfoKHR end_info = {};
			end_info.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
			vkCmdEndVideoCodingKHR(video_cmd, &end_info);

			if (dpb_output_coincide_supported)
			{
				if (dpb_layouts[video.current_slot] != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
				{
					// if current DPB slot is not in copy src layout, transition it now:
					VkImageMemoryBarrier barrier = {};
					barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					barrier.image = dpb_image;
					barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
					barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
					barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					barrier.subresourceRange.baseMipLevel = 0;
					barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
					barrier.subresourceRange.baseArrayLayer = video.current_slot;
					barrier.subresourceRange.layerCount = 1;
					barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					vkCmdPipelineBarrier(video_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
					dpb_layouts[video.current_slot] = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				}
			}
			else
			{
				// No-coincide output image needs will be used as copy src:
				VkImageMemoryBarrier barrier = {};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.image = decode_output_image;
				barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = 0;
				barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				vkCmdPipelineBarrier(video_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);
			}

			res = vkEndCommandBuffer(video_cmd);
			assert(res == VK_SUCCESS);

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.pCommandBuffers = &video_cmd;
			submitInfo.commandBufferCount = 1;
			submitInfo.pSignalSemaphores = &video_semaphore; // signals for graphics that it needs to wait for video decode to complete
			submitInfo.signalSemaphoreCount = 1;
			res = vkQueueSubmit(videoQueue, 1, &submitInfo, VK_NULL_HANDLE);
			assert(res == VK_SUCCESS);

			wait_semaphores.push_back(video_semaphore);
			printf("Decoded frame_index = %d, display_order: %d\n", video.frameIndex, frame_info.display_order);

			// DPB slot management:
			//	When current frame was a reference, then the next frame can not overwrite its DPB slot, so increment next_slot as a ring buffer
			//	However, the ring buffer will wrap around so older reference frames can be overwritten by this
			if (frame_info.reference_priority > 0)
			{
				if (video.next_ref >= video.reference_usage.size())
				{
					video.reference_usage.resize(video.next_ref + 1);
				}
				video.reference_usage[video.next_ref] = video.current_slot;
				video.next_ref = (video.next_ref + 1) % (video.num_dpb_slots - 1);
				video.next_slot = (video.next_slot + 1) % video.num_dpb_slots;
			}
		}

		// Resolve latest video frame to RGB onto the swapchain with a compute shader every frame, even if decoding didn't happen, the presentation loop is running independently of video decoder frame rate:
		res = vkResetCommandPool(device, graphics_command_pool, 0);
		assert(res == VK_SUCCESS);
		res = vkBeginCommandBuffer(graphics_cmd, &cmd_begin_info);
		assert(res == VK_SUCCESS);

		if (must_decode)
		{
			// If decode happened this frame, then copy the latest output to the reordering picture queue:
			if (reordered_results_free.empty())
			{
				// Request new image, because there is no more free ones that we can use:
				reordered_results_free.emplace_back();
				reordered_results_free.back().create(device, video.padded_width, video.padded_height);
			}

			// Copy will be done from decoder output to the newly allocated reordered picture:
			DecodeResultReordered reordered_current = reordered_results_free.back();
			reordered_results_free.pop_back();
			reordered_current.display_order = video.frame_infos[video.frameIndex].display_order;
			reordered_current.frame_index = video.frameIndex;

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.image = reordered_current.image;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			vkCmdPipelineBarrier(graphics_cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

			VkImageCopy cpy = {};
			cpy.extent.width = codedExtent.width;
			cpy.extent.height = codedExtent.height;
			cpy.extent.depth = 1;
			cpy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
			cpy.srcSubresource.layerCount = 1;
			cpy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
			cpy.dstSubresource.baseArrayLayer = 0;
			cpy.dstSubresource.layerCount = 1;
			VkImage src = VK_NULL_HANDLE;
			if (dpb_output_coincide_supported)
			{
				// Copy from DPB:
				src = dpb_image;
				cpy.srcSubresource.baseArrayLayer = video.current_slot;
			}
			else
			{
				// Copy from distinct output:
				src = decode_output_image;
				cpy.srcSubresource.baseArrayLayer = 0;
			}
			vkCmdCopyImage(graphics_cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, reordered_current.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
			cpy.extent.width = codedExtent.width / 2;
			cpy.extent.height = codedExtent.height / 2;
			cpy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
			cpy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
			vkCmdCopyImage(graphics_cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, reordered_current.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(graphics_cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

			// Current latest reordered is pushed to the reorder working queue:
			reordered_results_working.push_back(reordered_current);

			video.frameIndex = (video.frameIndex + 1) % video.frame_infos.size();
		}

		video.time_until_next_frame -= video.timer.elapsed_seconds();
		video.timer.record();
		if (video.time_until_next_frame <= 0)
		{
			// Search for the next displayable with lowest display order:
			int min = reordered_results_working[0].display_order;
			int mini = 0;
			for (int i = 1; i < (int)reordered_results_working.size(); ++i)
			{
				if (reordered_results_working[i].display_order < min)
				{
					min = reordered_results_working[i].display_order;
					mini = i;
				}
			}
			// Free current output texture:
			if (displayed_image.image != VK_NULL_HANDLE)
			{
				reordered_results_free.push_back(displayed_image);
			}
			// Take this used texture as current output:
			displayed_image = std::move(reordered_results_working[mini]);
			// Remove this used texture:
			std::swap(reordered_results_working[mini], reordered_results_working.back());
			reordered_results_working.pop_back();

			video.time_until_next_frame = video.frame_infos[displayed_image.frame_index].duration_seconds;

			assert(displayed_image.image != VK_NULL_HANDLE);
			printf("\tDisplayed image changed, frame_index: %d, display_order: %d\n", displayed_image.frame_index, displayed_image.display_order);
		}

		// Request free swapchain image:
		swapchain_acquire_semaphore_index = (swapchain_acquire_semaphore_index + 1) % (uint32_t)swapchain_acquire_semaphores.size();
		do {
			res = vkAcquireNextImageKHR(
				device,
				swapchain,
				~0ull,
				swapchain_acquire_semaphores[swapchain_acquire_semaphore_index],
				VK_NULL_HANDLE,
				&swapchain_image_index
			);
			assert(res >= VK_SUCCESS);
			if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
			{
				create_swapchain();
				res = VK_INCOMPLETE; // retry
			}
		} while (res != VK_SUCCESS);

		wait_semaphores.push_back(swapchain_acquire_semaphores[swapchain_acquire_semaphore_index]);

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = swapchain_images[swapchain_image_index];
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.srcAccessMask = VK_ACCESS_NONE;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkCmdPipelineBarrier(graphics_cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

		// Descriptors are updated for the YUV -> RGB resolving compute shader:
		res = vkResetDescriptorPool(device, descriptor_pool, 0);
		assert(res == VK_SUCCESS);
		VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
		VkDescriptorSetAllocateInfo descriptor_allocate_info = {};
		descriptor_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptor_allocate_info.descriptorPool = descriptor_pool;
		descriptor_allocate_info.descriptorSetCount = 1;
		descriptor_allocate_info.pSetLayouts = &descriptor_set_layout;
		res = vkAllocateDescriptorSets(device, &descriptor_allocate_info, &descriptor_set);
		assert(res == VK_SUCCESS);

		VkDescriptorImageInfo image_infos[3] = {};
		image_infos[0].imageView = displayed_image.image_view_luminance;
		image_infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[1].imageView = displayed_image.image_view_chrominance;
		image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		image_infos[2].imageView = swapchain_image_views[swapchain_image_index];
		image_infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet descriptor_writes[3] = {};
		descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		descriptor_writes[0].dstSet = descriptor_set;
		descriptor_writes[0].dstBinding = 1;
		descriptor_writes[0].descriptorCount = 1;
		descriptor_writes[0].pImageInfo = &image_infos[0];

		descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		descriptor_writes[1].dstSet = descriptor_set;
		descriptor_writes[1].dstBinding = 2;
		descriptor_writes[1].descriptorCount = 1;
		descriptor_writes[1].pImageInfo = &image_infos[1];

		descriptor_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptor_writes[2].dstSet = descriptor_set;
		descriptor_writes[2].dstBinding = 3;
		descriptor_writes[2].descriptorCount = 1;
		descriptor_writes[2].pImageInfo = &image_infos[2];
		vkUpdateDescriptorSets(device, arraysize(descriptor_writes), descriptor_writes, 0, nullptr);

		vkCmdBindDescriptorSets(graphics_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

		vkCmdBindPipeline(graphics_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

		vkCmdPushConstants(graphics_cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2, &video.width); // send width and height with push constants to shader

		// Dispatch compute shader with 8x8 threadgroup to resolve decode output Luminance + Chrominance image planes into RGB texture:
		//	(the shader directly writes into the swapchain texture)
		vkCmdDispatch(graphics_cmd, (swapchain_extent.width + 7u) / 8u, (swapchain_extent.height + 7u) / 8u, 1);

		barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_NONE;
		vkCmdPipelineBarrier(graphics_cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0, nullptr, 1, &barrier);

		res = vkEndCommandBuffer(graphics_cmd);
		assert(res == VK_SUCCESS);

		// Submit graphics queue and present:

		VkPipelineStageFlags wait_pipeline_stage[] = {
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		};

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pCommandBuffers = &graphics_cmd;
		submitInfo.commandBufferCount = 1;
		submitInfo.pSignalSemaphores = &swapchain_release_semaphore;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = wait_semaphores.data();
		submitInfo.waitSemaphoreCount = (uint32_t)wait_semaphores.size();
		submitInfo.pWaitDstStageMask = wait_pipeline_stage;
		res = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence); // fence is always signaled by graphics
		assert(res == VK_SUCCESS);

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pWaitSemaphores = &swapchain_release_semaphore;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &swapchain_image_index;
		res = vkQueuePresentKHR(graphicsQueue, &presentInfo);
		assert(res >= VK_SUCCESS);
		if (res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			create_swapchain();
		}

		// In this sample, I always wait for GPU completion on the CPU to simplify command buffer and descriptor management:
		res = vkWaitForFences(device, 1, &fence, VK_TRUE, ~0ull);
		assert(res == VK_SUCCESS);

		res = vkResetFences(device, 1, &fence);
		assert(res == VK_SUCCESS);
	}

	// Clean up everything:
	vkDeviceWaitIdle(device);
	vkDestroyBuffer(device, bitstream_buffer, nullptr);
	vkFreeMemory(device, bitstream_buffer_memory, nullptr);
	vkDestroyImageView(device, dpb_image_view, nullptr);
	vkDestroyImage(device, dpb_image, nullptr);
	vkFreeMemory(device, dpb_image_memory, nullptr);
	vkDestroyImageView(device, decode_output_image_view, nullptr);
	vkDestroyImage(device, decode_output_image, nullptr);
	vkFreeMemory(device, decode_output_image_memory, nullptr);
	for (auto& x : reordered_results_free)
	{
		x.destroy(device);
	}
	for (auto& x : reordered_results_working)
	{
		x.destroy(device);
	}
	displayed_image.destroy(device);
	vkDestroyVideoSessionKHR(device, video_session, nullptr);
	for (auto& x : video_session_allocations)
	{
		vkFreeMemory(device, x, nullptr);
	}
	vkDestroyVideoSessionParametersKHR(device, session_parameters, nullptr);
	vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
	vkDestroyShaderModule(device, shader_module, nullptr);
	vkDestroyPipeline(device, pipeline, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
	vkDestroySampler(device, sampler, nullptr);
	vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
	for (auto& x : swapchain_image_views)
	{
		vkDestroyImageView(device, x, nullptr);
	}
	for (auto& x : swapchain_acquire_semaphores)
	{
		vkDestroySemaphore(device, x, nullptr);
	}
	vkDestroySemaphore(device, swapchain_release_semaphore, nullptr);
	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroyCommandPool(device, video_command_pool, nullptr);
	vkDestroyCommandPool(device, graphics_command_pool, nullptr);
	vkDestroySemaphore(device, video_semaphore, nullptr);
	vkDestroyFence(device, fence, nullptr);
	vkDestroyDevice(device, nullptr);
	if (debugUtilsMessenger != VK_NULL_HANDLE)
	{
		vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
	}

	return 0;
}
