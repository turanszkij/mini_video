#include "include/common.h"

#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <dxva.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif // DEBUG
#pragma comment(lib,"dxguid.lib")

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

	// Below this will be all the DirectX 12 code:
	using namespace Microsoft::WRL;
	HRESULT hr;
	ComPtr<IDXGIFactory4> dxgiFactory;
	ComPtr<IDXGIAdapter1> dxgiAdapter;
	ComPtr<ID3D12Device5> device;
	ComPtr<ID3D12VideoDevice> video_device;
	ComPtr<ID3D12DescriptorHeap> descriptor_heap;

	// Create device:
	{
		HMODULE dxgi = LoadLibraryEx(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		assert(dxgi != nullptr);
		using PFN_CREATE_DXGI_FACTORY_2 = decltype(&CreateDXGIFactory2);
		static PFN_CREATE_DXGI_FACTORY_2 CreateDXGIFactory2 = (PFN_CREATE_DXGI_FACTORY_2)GetProcAddress(dxgi, "CreateDXGIFactory2");
		assert(CreateDXGIFactory2 != nullptr);
#ifdef _DEBUG
		using PFN_DXGI_GET_DEBUG_INTERFACE1 = decltype(&DXGIGetDebugInterface1);
		static PFN_DXGI_GET_DEBUG_INTERFACE1 DXGIGetDebugInterface1 = (PFN_DXGI_GET_DEBUG_INTERFACE1)GetProcAddress(dxgi, "DXGIGetDebugInterface1");
		assert(DXGIGetDebugInterface1 != nullptr);
#endif // _DEBUG

		HMODULE dx12 = LoadLibraryEx(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		assert(dx12 != nullptr);
		static PFN_D3D12_CREATE_DEVICE D3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(dx12, "D3D12CreateDevice");
		assert(D3D12CreateDevice != nullptr);
		PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER D3D12CreateVersionedRootSignatureDeserializer = (PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER)GetProcAddress(dx12, "D3D12CreateVersionedRootSignatureDeserializer");
		assert(D3D12CreateVersionedRootSignatureDeserializer != nullptr);

#ifdef _DEBUG
		static PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)GetProcAddress(dx12, "D3D12GetDebugInterface");
		if (D3D12GetDebugInterface)
		{
			ComPtr<ID3D12Debug> d3dDebug;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d3dDebug))))
			{
				d3dDebug->EnableDebugLayer();
			}
		}
		ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
		if (DXGIGetDebugInterface1 != nullptr && SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
		{
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
			dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
		}
#endif // _DEBUG

#ifdef _DEBUG
		hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory));
#else
		hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
#endif // _DEBUG
		assert(SUCCEEDED(hr));

		ComPtr<IDXGIFactory6> dxgiFactory6;
		const bool queryByPreference = SUCCEEDED(dxgiFactory.As(&dxgiFactory6));
		auto NextAdapter = [&](uint32_t index, IDXGIAdapter1** ppAdapter) {
			if (queryByPreference)
			{
				return dxgiFactory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(ppAdapter));
			}
			return dxgiFactory->EnumAdapters1(index, ppAdapter);
			};
		for (uint32_t i = 0; NextAdapter(i, dxgiAdapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 adapterDesc;
			hr = dxgiAdapter->GetDesc1(&adapterDesc);
			if (SUCCEEDED(hr))
			{
				// Don't select the Basic Render Driver adapter.
				if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					continue;
				}
			}

			D3D_FEATURE_LEVEL featurelevels[] = {
				D3D_FEATURE_LEVEL_12_2,
				D3D_FEATURE_LEVEL_12_1,
				D3D_FEATURE_LEVEL_12_0,
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
			};
			for (auto& featureLevel : featurelevels)
			{
				if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter.Get(), featureLevel, IID_PPV_ARGS(&device))))
				{
					break;
				}
			}
			if (device != nullptr)
				break;
		}
		assert(dxgiAdapter != nullptr);
		assert(device != nullptr);

#ifdef _DEBUG
		ComPtr<ID3D12InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(device.As(&d3dInfoQueue)))
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		}
#endif // _DEBUG

		if (SUCCEEDED(device.As(&video_device)))
		{
			D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT video_decode_profile_count = {};
			hr = video_device->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT, &video_decode_profile_count, sizeof(video_decode_profile_count));
			assert(SUCCEEDED(hr));
			std::vector<GUID> video_decode_profile_list;
			video_decode_profile_list.resize(video_decode_profile_count.ProfileCount);
			D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES video_decode_profiles = {};
			video_decode_profiles.ProfileCount = video_decode_profile_count.ProfileCount;
			video_decode_profiles.pProfiles = video_decode_profile_list.data();
			hr = video_device->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROFILES, &video_decode_profiles, sizeof(video_decode_profiles));
			assert(SUCCEEDED(hr));
			bool h264_supported = false;
			for (auto& profile : video_decode_profile_list)
			{
				if (profile == D3D12_VIDEO_DECODE_PROFILE_H264)
				{
					h264_supported = true;
					break;
				}
			}
			if (!h264_supported)
			{
				printf("The H264 video decoding is not supported by your GPU, exiting!");
				return -2;
			}
		}

		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptor_heap_desc.NumDescriptors = 3;
		hr = device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap));
		assert(SUCCEEDED(hr));
	}

	// Create video decoder:
	ComPtr<ID3D12VideoDecoder> decoder;
	ComPtr<ID3D12VideoDecoderHeap> decoder_heap;
	const DXGI_FORMAT decode_format = DXGI_FORMAT_NV12; // Format with Luminance and Chrominance planes
	bool reference_only_allocation = false; // indicates that DPB texture cannot be used for anything else if true (AMD is true, Nvidia is false with the cards I checked so both cases need to be handled)
	{
		D3D12_VIDEO_DECODER_DESC decoder_desc = {};
		decoder_desc.Configuration.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_H264;
		decoder_desc.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;

		D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT video_decode_format_count = {};
		video_decode_format_count.Configuration = decoder_desc.Configuration;
		hr = video_device->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT, &video_decode_format_count, sizeof(video_decode_format_count));
		assert(SUCCEEDED(hr));
		std::vector<DXGI_FORMAT> formats(video_decode_format_count.FormatCount);
		D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS video_decode_formats = {};
		video_decode_formats.Configuration = decoder_desc.Configuration;
		video_decode_formats.FormatCount = video_decode_format_count.FormatCount;
		video_decode_formats.pOutputFormats = formats.data();
		hr = video_device->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_FORMATS, &video_decode_formats, sizeof(video_decode_formats));
		assert(SUCCEEDED(hr));
		D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT video_decode_support = {};
		video_decode_support.Configuration = decoder_desc.Configuration;
		video_decode_support.DecodeFormat = decode_format;
		bool format_valid = false;
		for (auto& x : formats)
		{
			if (x == video_decode_support.DecodeFormat)
			{
				format_valid = true;
				break;
			}
		}
		if (!format_valid)
		{
			printf("Haven't found an appropriate decoder configuration for the selected format, exiting!");
			return -3;
		}
		video_decode_support.Width = video.padded_width;
		video_decode_support.Height = video.padded_height;
		video_decode_support.BitRate = 0;
		video_decode_support.FrameRate = { 0, 1 };
		hr = video_device->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &video_decode_support, sizeof(video_decode_support));
		assert(SUCCEEDED(hr));

		if (video_decode_support.DecodeTier < D3D12_VIDEO_DECODE_TIER_1)
		{
			printf("D3D12_VIDEO_DECODE_TIER_1 is not supported for the selected configuration, exiting!");
			return -4;
		}

		reference_only_allocation = video_decode_support.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED;

		D3D12_VIDEO_DECODER_HEAP_DESC heap_desc = {};
		heap_desc.Configuration = decoder_desc.Configuration;
		heap_desc.DecodeWidth = video_decode_support.Width;
		heap_desc.DecodeHeight = video_decode_support.Height;
		heap_desc.Format = video_decode_support.DecodeFormat;
		heap_desc.FrameRate = { 0,1 };
		heap_desc.BitRate = 0;
		heap_desc.MaxDecodePictureBufferCount = video.num_dpb_slots;

		if (video_decode_support.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED)
		{
			heap_desc.DecodeWidth = align(video_decode_support.Width, 32u);
			heap_desc.DecodeHeight = align(video_decode_support.Height, 32u);
		}

		hr = video_device->CreateVideoDecoderHeap(&heap_desc, IID_PPV_ARGS(&decoder_heap));
		assert(SUCCEEDED(hr));
		hr = video_device->CreateVideoDecoder(&decoder_desc, IID_PPV_ARGS(&decoder));
		assert(SUCCEEDED(hr));
	}

	// Create bitstream GPU buffer and copy the H264 data into it:
	ComPtr<ID3D12Resource> bitstream_buffer;
	{
		uint64_t aligned_size = 0;
		for (Video::FrameInfo& frame_info : video.frame_infos)
		{
			aligned_size += align(frame_info.size, (uint64_t)D3D12_VIDEO_DECODE_MIN_BITSTREAM_OFFSET_ALIGNMENT);
		}

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Width = aligned_size;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.MipLevels = 1;
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&bitstream_buffer));
		assert(SUCCEEDED(hr));

		void* mapped_data = nullptr;
		hr = bitstream_buffer->Map(0, nullptr, &mapped_data);
		assert(SUCCEEDED(hr));

		// Write the slice datas into the aligned offsets, and store the aligned offsets in frame_infos, from here they will be storing offsets into the bitstream buffer, and not the source file:
		uint64_t aligned_offset = 0;
		for (Video::FrameInfo& frame_info : video.frame_infos)
		{
			std::memcpy((uint8_t*)mapped_data + aligned_offset, video.h264_data.data() + frame_info.offset, frame_info.size); // copy into GPU buffer through mapped_data ptr
			frame_info.offset = aligned_offset;
			aligned_offset += align(frame_info.size, (uint64_t)D3D12_VIDEO_DECODE_MIN_BITSTREAM_OFFSET_ALIGNMENT);
		}

		// The h264_data is not required any longer to be in RAM because it has been copied to the GPU buffer, so I delete it:
		video.h264_data.clear();

		bitstream_buffer->Unmap(0, nullptr);
	}

	// Create Decoded Picture Buffer (DPB) texture:
	//	NOTE: Nvidia and Intel will crash or produce corruption if the DPB is not a Committed Resource!
	ComPtr<ID3D12Resource> dpb_texture;
	D3D12_RESOURCE_STATES dpb_layouts[17] = {};
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Format = decode_format;
		desc.Width = video.padded_width;
		desc.Height = video.padded_height;
		desc.DepthOrArraySize = video.num_dpb_slots;
		desc.SampleDesc.Count = 1;
		desc.MipLevels = 1;
		if (reference_only_allocation)
		{
			desc.Flags = D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		}
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&dpb_texture));
		assert(SUCCEEDED(hr));
	}

	// If D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY is required, then there must be output conversion in the decoder:
	ComPtr<ID3D12Resource> dpb_output_texture;
	if (reference_only_allocation)
	{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Format = decode_format;
		desc.Width = video.padded_width;
		desc.Height = video.padded_height;
		desc.DepthOrArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.MipLevels = 1;
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&dpb_output_texture));
		assert(SUCCEEDED(hr));
	}

	struct DecodeResultReordered
	{
		ComPtr<ID3D12Resource> texture;
		int display_order = -1;
		int frame_index = 0;
		void create(ID3D12Device* device, uint32_t padded_width, uint32_t padded_height)
		{
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			desc.Format = decode_format;
			desc.Width = padded_width;
			desc.Height = padded_height;
			desc.DepthOrArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.MipLevels = 1;
			D3D12_HEAP_PROPERTIES heap_properties = {};
			heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture));
			assert(SUCCEEDED(hr));
		}
	};
	std::vector<DecodeResultReordered> reordered_results_free; // free image slots that can be reused as reordering image displaying
	std::vector<DecodeResultReordered> reordered_results_working; // image slots that are currently in use for reordering
	DecodeResultReordered displayed_image; // the latest reordered picture

	// Create shader:
	ComPtr<ID3D12PipelineState> compute_pso;
	ComPtr<ID3D12RootSignature> rootsignature;
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

				hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
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
					// vulkan attribute warnings are silenced
					L"-Wno-ignored-attributes",

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
					return -1;
				}

				Microsoft::WRL::ComPtr<IDxcBlob> pShader = nullptr;
				hr = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), nullptr);
				assert(SUCCEEDED(hr));
				if (pShader != nullptr)
				{
					D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
					desc.CS.pShaderBytecode = pShader->GetBufferPointer();
					desc.CS.BytecodeLength = pShader->GetBufferSize();
					hr = device->CreateRootSignature(
						0,
						desc.CS.pShaderBytecode,
						desc.CS.BytecodeLength,
						IID_PPV_ARGS(&rootsignature)
					);
					assert(SUCCEEDED(hr));
					hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&compute_pso));
					assert(SUCCEEDED(hr));
				}
			}
		}
	}

	// Create command buffers, etc:
	ComPtr<ID3D12CommandQueue> graphics_queue;
	ComPtr<ID3D12CommandQueue> video_queue;
	ComPtr<ID3D12CommandAllocator> graphics_command_allocator;
	ComPtr<ID3D12CommandAllocator> video_command_allocator;
	ComPtr<ID3D12GraphicsCommandList> graphics_cmd;
	ComPtr<ID3D12VideoDecodeCommandList> video_cmd;
	ComPtr<ID3D12Fence> graphics_fence;
	ComPtr<ID3D12Fence> video_fence;
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&graphics_queue));
		assert(SUCCEEDED(hr));
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
		hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&video_queue));
		assert(SUCCEEDED(hr));
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&graphics_command_allocator));
		assert(SUCCEEDED(hr));
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, IID_PPV_ARGS(&video_command_allocator));
		assert(SUCCEEDED(hr));
		hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&graphics_cmd));
		assert(SUCCEEDED(hr));
		hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&video_cmd));
		assert(SUCCEEDED(hr));
		device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&graphics_fence));
		assert(SUCCEEDED(hr));
		device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&video_fence));
		assert(SUCCEEDED(hr));
	}

	// Create window to display video onto:
#ifdef _WIN32
	HINSTANCE hInstance = NULL;
	static bool request_swapchain_resize = false;
	HWND hWnd = NULL;
	{
		static auto WndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
		{
			switch (message)
			{
			case WM_SIZE:
				request_swapchain_resize = true;
				break;
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
		wcex.lpszClassName = L"mini_video_dx12";
		wcex.hIconSm = NULL;
		RegisterClassExW(&wcex);
		hWnd = CreateWindowW(L"mini_video_dx12", L"mini_video_dx12", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, video.width, video.height, nullptr, nullptr, NULL, nullptr);
		ShowWindow(hWnd, SW_SHOWDEFAULT);
	}
#endif // _WIN32

	// Create swapchain for the window:
	ComPtr<IDXGISwapChain3> swapchain;
	ComPtr<ID3D12Resource> swapchain_resources[2];
	ComPtr<ID3D12Resource> swapchain_uav; // this is an unordered access view with the same parameters as swapchain, because we cannot directly write into swapchain from compute
	UINT swapchain_width = 0;
	UINT swapchain_height = 0;
	DXGI_FORMAT swapchain_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	auto create_swapchain = [&]() { // because the swapchain will be recreated on window resize, I create a resusable lambda function for it
		if (swapchain != nullptr)
		{
			// Wait for GPU to become idle if recreating:
			ComPtr<ID3D12Fence> fence;
			hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
			assert(SUCCEEDED(hr));
			hr = graphics_queue->Signal(fence.Get(), 1);
			assert(SUCCEEDED(hr));
			hr = fence->SetEventOnCompletion(1, nullptr);
			assert(SUCCEEDED(hr));
			swapchain.Reset();
			for (auto& x : swapchain_resources)
			{
				x.Reset();
			}
		}

		RECT rect = {};
		GetClientRect(hWnd, &rect);
		swapchain_width = UINT(rect.right - rect.left);
		swapchain_height = UINT(rect.bottom - rect.top);

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = swapchain_width;
		swapChainDesc.Height = swapchain_height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = false;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = arraysize(swapchain_resources);
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Flags = 0;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc = {};
		fullscreenDesc.Windowed = TRUE;

		ComPtr<IDXGISwapChain1> tempSwapChain;
		hr = dxgiFactory->CreateSwapChainForHwnd(
			graphics_queue.Get(),
			hWnd,
			&swapChainDesc,
			&fullscreenDesc,
			nullptr,
			tempSwapChain.ReleaseAndGetAddressOf()
		);
		assert(SUCCEEDED(hr));

		hr = tempSwapChain.As(&swapchain);
		assert(SUCCEEDED(hr));

		for (UINT i = 0; i < swapChainDesc.BufferCount; ++i)
		{
			hr = swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_resources[i]));
			assert(SUCCEEDED(hr));
		}

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Format = swapChainDesc.Format;
		desc.Width = swapChainDesc.Width;
		desc.Height = swapChainDesc.Height;
		desc.DepthOrArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.MipLevels = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		D3D12_HEAP_PROPERTIES heap_properties = {};
		heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&swapchain_uav));
		assert(SUCCEEDED(hr));

		printf("swapchain resized, new size: %d x %d\n", (int)swapchain_width, (int)swapchain_height);
	};
	create_swapchain();

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
			if (request_swapchain_resize)
			{
				create_swapchain();
				request_swapchain_resize = false;
			}
			continue;
		}
#endif // WIN32

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

			hr = video_command_allocator->Reset();
			assert(SUCCEEDED(hr));
			hr = video_cmd->Reset(video_command_allocator.Get());
			assert(SUCCEEDED(hr));

			if (dpb_layouts[video.current_slot] != D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE)
			{
				// if current DPB slot is not in DPB layout, transition it now:
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = dpb_texture.Get();
				barrier.Transition.StateBefore = dpb_layouts[video.current_slot];
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
				barrier.Transition.Subresource = video.current_slot;
				video_cmd->ResourceBarrier(1, &barrier);
				dpb_layouts[video.current_slot] = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
			}
			if (reference_only_allocation)
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = dpb_output_texture.Get();
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
				video_cmd->ResourceBarrier(1, &barrier);
			}

			auto D3D12CalcSubresource = [](UINT MipSlice, UINT ArraySlice, UINT PlaneSlice, UINT MipLevels, UINT ArraySize) noexcept { return MipSlice + ArraySlice * MipLevels + PlaneSlice * MipLevels * ArraySize; };

			D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS input = {};
			D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS output = {};
			if (reference_only_allocation)
			{
				output.pOutputTexture2D = dpb_output_texture.Get();
				output.OutputSubresource = 0;
				output.ConversionArguments.Enable = TRUE;
				output.ConversionArguments.pReferenceTexture2D = dpb_texture.Get();
				output.ConversionArguments.ReferenceSubresource = video.current_slot;
			}
			else
			{
				output.pOutputTexture2D = dpb_texture.Get();
				output.OutputSubresource = video.current_slot;
			}

			ID3D12Resource* reference_frames[16] = {};
			UINT reference_subresources[16] = {};
			for (size_t i = 0; i < video.num_dpb_slots; ++i)
			{
				reference_frames[i] = dpb_texture.Get();
				reference_subresources[i] = (UINT)i;
			}
			input.ReferenceFrames.NumTexture2Ds = arraysize(reference_frames);
			input.ReferenceFrames.ppTexture2Ds = reference_frames;
			input.ReferenceFrames.pSubresources = reference_subresources;

			input.CompressedBitstream.pBuffer = bitstream_buffer.Get();
			input.CompressedBitstream.Offset = frame_info.offset;
			input.CompressedBitstream.Size = frame_info.size;
			input.pHeap = decoder_heap.Get();

			DXVA_PicParams_H264 pic_params_h264 = {};
			DXVA_Qmatrix_H264 qmatrix_h264 = {};
			DXVA_Slice_H264_Short sliceinfo_h264 = {};

			// DirectX Video Acceleration for H.264/MPEG-4 AVC Decoding, Microsoft, Updated 2010, Page 21
			//	https://www.microsoft.com/en-us/download/details.aspx?id=11323
			//	Also: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/d3d12/d3d12_video_dec_h264.cpp
			//	Also: https://github.com/mofo7777/H264Dxva2Decoder
			pic_params_h264.wFrameWidthInMbsMinus1 = sps.pic_width_in_mbs_minus1;
			pic_params_h264.wFrameHeightInMbsMinus1 = sps.pic_height_in_map_units_minus1;
			pic_params_h264.IntraPicFlag = frame_info.is_intra ? 1 : 0;
			pic_params_h264.MbaffFrameFlag = 0 /*sps->mb_adaptive_frame_field_flag && !slice_header->field_pic_flag*/;
			pic_params_h264.field_pic_flag = 0 /*slice_header->field_pic_flag*/; // 0 = full frame (top and bottom field)
			//pic_params_h264.bottom_field_flag = 0; // missing??
			pic_params_h264.chroma_format_idc = 1; // sps->chroma_format_idc; // only 1 is supported (YUV420)
			pic_params_h264.bit_depth_chroma_minus8 = sps.bit_depth_chroma_minus8;
			assert(pic_params_h264.bit_depth_chroma_minus8 == 0);   // Only support for NV12 now
			pic_params_h264.bit_depth_luma_minus8 = sps.bit_depth_luma_minus8;
			assert(pic_params_h264.bit_depth_luma_minus8 == 0);   // Only support for NV12 now
			pic_params_h264.residual_colour_transform_flag = sps.separate_colour_plane_flag; // https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/d3d12/d3d12_video_dec_h264.cpp#L328
			if (pic_params_h264.field_pic_flag)
			{
				pic_params_h264.CurrPic.AssociatedFlag = slice_header.bottom_field_flag ? 1 : 0; // if pic_params_h264.field_pic_flag == 1, then this is 0 = top field, 1 = bottom_field
			}
			else
			{
				pic_params_h264.CurrPic.AssociatedFlag = 0;
			}
			pic_params_h264.CurrPic.Index7Bits = (UCHAR)video.current_slot;
			pic_params_h264.CurrFieldOrderCnt[0] = frame_info.poc;
			pic_params_h264.CurrFieldOrderCnt[1] = frame_info.poc;
			for (uint32_t i = 0; i < 16; ++i)
			{
				pic_params_h264.RefFrameList[i].bPicEntry = 0xFF;
				pic_params_h264.FieldOrderCntList[i][0] = 0;
				pic_params_h264.FieldOrderCntList[i][1] = 0;
				pic_params_h264.FrameNumList[i] = 0;
			}
			for (size_t i = 0; i < video.reference_usage.size(); ++i)
			{
				uint32_t ref_slot = video.reference_usage[i];
				assert(ref_slot != video.current_slot);
				pic_params_h264.RefFrameList[i].AssociatedFlag = 0; // 0 = short term, 1 = long term reference
				pic_params_h264.RefFrameList[i].Index7Bits = (UCHAR)ref_slot;
				pic_params_h264.FieldOrderCntList[i][0] = video.poc_status[ref_slot];
				pic_params_h264.FieldOrderCntList[i][1] = video.poc_status[ref_slot];
				pic_params_h264.UsedForReferenceFlags |= 1 << (i * 2 + 0);
				pic_params_h264.UsedForReferenceFlags |= 1 << (i * 2 + 1);
				pic_params_h264.FrameNumList[i] = video.framenum_status[ref_slot];
			}
			pic_params_h264.weighted_pred_flag = pps.weighted_pred_flag;
			pic_params_h264.weighted_bipred_idc = pps.weighted_bipred_idc;
			pic_params_h264.sp_for_switch_flag = 0;

			pic_params_h264.transform_8x8_mode_flag = pps.transform_8x8_mode_flag;
			pic_params_h264.constrained_intra_pred_flag = pps.constrained_intra_pred_flag;
			pic_params_h264.num_ref_frames = sps.num_ref_frames;
			pic_params_h264.MbsConsecutiveFlag = 1; // The value shall be 1 unless the restricted-mode profile in use explicitly supports the value 0.
			pic_params_h264.frame_mbs_only_flag = sps.frame_mbs_only_flag;
			pic_params_h264.MinLumaBipredSize8x8Flag = sps.level_idc >= 31;
			pic_params_h264.RefPicFlag = frame_info.reference_priority > 0 ? 1 : 0;
			pic_params_h264.frame_num = slice_header.frame_num;
			pic_params_h264.pic_init_qp_minus26 = pps.pic_init_qp_minus26;
			pic_params_h264.pic_init_qs_minus26 = pps.pic_init_qs_minus26;
			pic_params_h264.chroma_qp_index_offset = pps.chroma_qp_index_offset;
			pic_params_h264.second_chroma_qp_index_offset = pps.second_chroma_qp_index_offset;
			pic_params_h264.log2_max_frame_num_minus4 = sps.log2_max_frame_num_minus4;
			pic_params_h264.pic_order_cnt_type = sps.pic_order_cnt_type;
			pic_params_h264.log2_max_pic_order_cnt_lsb_minus4 = sps.log2_max_pic_order_cnt_lsb_minus4;
			pic_params_h264.delta_pic_order_always_zero_flag = sps.delta_pic_order_always_zero_flag;
			pic_params_h264.direct_8x8_inference_flag = sps.direct_8x8_inference_flag;
			pic_params_h264.entropy_coding_mode_flag = pps.entropy_coding_mode_flag;
			pic_params_h264.pic_order_present_flag = pps.pic_order_present_flag;
			pic_params_h264.num_slice_groups_minus1 = pps.num_slice_groups_minus1;
			assert(pic_params_h264.num_slice_groups_minus1 == 0);   // FMO Not supported by VA
			pic_params_h264.slice_group_map_type = pps.slice_group_map_type;
			pic_params_h264.deblocking_filter_control_present_flag = pps.deblocking_filter_control_present_flag;
			pic_params_h264.redundant_pic_cnt_present_flag = pps.redundant_pic_cnt_present_flag;
			pic_params_h264.slice_group_change_rate_minus1 = pps.slice_group_change_rate_minus1;
			pic_params_h264.Reserved16Bits = 3; // DXVA spec
			pic_params_h264.StatusReportFeedbackNumber = (UINT)video.frameIndex + 1; // shall not be 0
			assert(pic_params_h264.StatusReportFeedbackNumber > 0);
			pic_params_h264.ContinuationFlag = 1;
			pic_params_h264.num_ref_idx_l0_active_minus1 = pps.num_ref_idx_l0_active_minus1;
			pic_params_h264.num_ref_idx_l1_active_minus1 = pps.num_ref_idx_l1_active_minus1;
			input.FrameArguments[input.NumFrameArguments].Type = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS;
			input.FrameArguments[input.NumFrameArguments].Size = sizeof(pic_params_h264);
			input.FrameArguments[input.NumFrameArguments].pData = &pic_params_h264;
			input.NumFrameArguments++;

			// DirectX Video Acceleration for H.264/MPEG-4 AVC Decoding, Microsoft, Updated 2010, Page 29
			//	Also: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/d3d12/d3d12_video_dec_h264.cpp#L548
			if (sps.seq_scaling_matrix_present_flag)
			{
				static constexpr int vl_zscan_normal_16[] =
				{
					/* Zig-Zag scan pattern */
					0, 1, 4, 8, 5, 2, 3, 6,
					9, 12, 13, 10, 7, 11, 14, 15
				};
				static constexpr int vl_zscan_normal[] =
				{
					/* Zig-Zag scan pattern */
					 0, 1, 8,16, 9, 2, 3,10,
					17,24,32,25,18,11, 4, 5,
					12,19,26,33,40,48,41,34,
					27,20,13, 6, 7,14,21,28,
					35,42,49,56,57,50,43,36,
					29,22,15,23,30,37,44,51,
					58,59,52,45,38,31,39,46,
					53,60,61,54,47,55,62,63
				};
				for (int i = 0; i < 6; ++i)
				{
					for (int j = 0; j < 16; ++j)
					{
						qmatrix_h264.bScalingLists4x4[i][j] = pps.ScalingList4x4[i][vl_zscan_normal_16[j]];
					}
				}
				for (int i = 0; i < 64; ++i)
				{
					qmatrix_h264.bScalingLists8x8[0][i] = pps.ScalingList8x8[0][vl_zscan_normal[i]];
					qmatrix_h264.bScalingLists8x8[1][i] = pps.ScalingList8x8[1][vl_zscan_normal[i]];
				}
			}
			else
			{
				// I don't know why it needs to be filled with 16, but otherwise it gets corrupted output
				//	Source: https://github.com/mofo7777/H264Dxva2Decoder
				std::memset(&qmatrix_h264, 16, sizeof(DXVA_Qmatrix_H264));
			}
			input.FrameArguments[input.NumFrameArguments].Type = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX;
			input.FrameArguments[input.NumFrameArguments].Size = sizeof(qmatrix_h264);
			input.FrameArguments[input.NumFrameArguments].pData = &qmatrix_h264;
			input.NumFrameArguments++;

			// DirectX Video Acceleration for H.264/MPEG-4 AVC Decoding, Microsoft, Updated 2010, Page 31
			sliceinfo_h264.BSNALunitDataLocation = 0;
			sliceinfo_h264.SliceBytesInBuffer = (UINT)frame_info.size;
			sliceinfo_h264.wBadSliceChopping = 0; // whole slice is in the buffer
			input.FrameArguments[input.NumFrameArguments].Type = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
			input.FrameArguments[input.NumFrameArguments].Size = sizeof(sliceinfo_h264);
			input.FrameArguments[input.NumFrameArguments].pData = &sliceinfo_h264;
			input.NumFrameArguments++;

			video_cmd->DecodeFrame(decoder.Get(), &output, &input);

			if (reference_only_allocation)
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = dpb_output_texture.Get();
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
				video_cmd->ResourceBarrier(1, &barrier);
			}
			else
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = dpb_texture.Get();
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
				barrier.Transition.Subresource = video.current_slot;
				video_cmd->ResourceBarrier(1, &barrier);
				dpb_layouts[video.current_slot] = D3D12_RESOURCE_STATE_COMMON;
			}

			hr = video_cmd->Close();
			assert(SUCCEEDED(hr));
			ID3D12CommandList* commandlists[] = { video_cmd.Get() };
			video_queue->ExecuteCommandLists(arraysize(commandlists), commandlists);
			hr = video_queue->Signal(video_fence.Get(), 1); // signal for graphics queue, GPU will wait for this fence
			assert(SUCCEEDED(hr)); 
			
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

		hr = graphics_command_allocator->Reset();
		assert(SUCCEEDED(hr));
		hr = graphics_cmd->Reset(graphics_command_allocator.Get(), nullptr);
		assert(SUCCEEDED(hr));

		if (must_decode)
		{
			// If decode happened this frame, then copy the latest output to the reordering picture queue:
			if (reordered_results_free.empty())
			{
				// Request new image, because there is no more free ones that we can use:
				reordered_results_free.emplace_back().create(device.Get(), video.padded_width, video.padded_height);
			}

			// Copy will be done from decoder output to the newly allocated reordered picture:
			DecodeResultReordered reordered_current = reordered_results_free.back();
			reordered_results_free.pop_back();
			reordered_current.display_order = video.frame_infos[video.frameIndex].display_order;
			reordered_current.frame_index = video.frameIndex;

			D3D12_TEXTURE_COPY_LOCATION cpy_src = {};
			cpy_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			if (reference_only_allocation)
			{
				cpy_src.pResource = dpb_output_texture.Get();
			}
			else
			{
				cpy_src.pResource = dpb_texture.Get();
				cpy_src.SubresourceIndex = video.current_slot;
			}
			D3D12_TEXTURE_COPY_LOCATION cpy_dst = {};
			cpy_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			cpy_dst.pResource = reordered_current.texture.Get();

			cpy_src.SubresourceIndex = 0; // luma plane
			cpy_dst.SubresourceIndex = 0; // luma plane
			graphics_cmd->CopyTextureRegion(&cpy_dst, 0, 0, 0, &cpy_src, nullptr);

			cpy_src.SubresourceIndex = 1; // chroma plane
			cpy_dst.SubresourceIndex = 1; // chroma plane
			graphics_cmd->CopyTextureRegion(&cpy_dst, 0, 0, 0, &cpy_src, nullptr);

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
			if (displayed_image.texture != nullptr)
			{
				reordered_results_free.push_back(displayed_image);
			}
			// Take this used texture as current output:
			displayed_image = std::move(reordered_results_working[mini]);
			// Remove this used texture:
			std::swap(reordered_results_working[mini], reordered_results_working.back());
			reordered_results_working.pop_back();

			video.time_until_next_frame = video.frame_infos[displayed_image.frame_index].duration_seconds;

			assert(displayed_image.texture != nullptr);
			printf("\tDisplayed image changed, frame_index: %d, display_order: %d\n", displayed_image.frame_index, displayed_image.display_order);
		}

		// Resolve video with compute shader:
		{
			// the displayed image's two planes are put into shader resource state, the swapchain_uav is put into unordered access state:
			D3D12_RESOURCE_BARRIER barriers[3] = {};
			barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[0].Transition.pResource = displayed_image.texture.Get();
			barriers[0].Transition.Subresource = 0; // luma plane
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[1].Transition.pResource = displayed_image.texture.Get();
			barriers[1].Transition.Subresource = 1; // chroma plane
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[2].Transition.pResource = swapchain_uav.Get();
			barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
			barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			graphics_cmd->ResourceBarrier(arraysize(barriers), barriers);

			graphics_cmd->DiscardResource(swapchain_uav.Get(), nullptr); // previous contents are not retained, UAV will be fully overwritten

			// Set up all descriptors:
			//	This setup is hardcoded for the yuv_to_rgbCS.hlsl shader's root signature layout
			//	There is a single DescriptorTable, with 2 SRVs followed by 1 UAV
			//	The descriptors are created into the shader visible descriptor heap every frame
			ID3D12DescriptorHeap* descriptor_heaps[] = { descriptor_heap.Get() };
			graphics_cmd->SetDescriptorHeaps(arraysize(descriptor_heaps), descriptor_heaps);
			graphics_cmd->SetComputeRootSignature(rootsignature.Get());
			graphics_cmd->SetPipelineState(compute_pso.Get());
			D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
			const UINT descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Format = DXGI_FORMAT_R8_UNORM;
			srv_desc.Texture2D.MipLevels = 1;
			srv_desc.Texture2D.PlaneSlice = 0;
			device->CreateShaderResourceView(displayed_image.texture.Get(), &srv_desc, cpu_handle);	// Texture2D<float> input_luminance : register(t0);
			srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
			srv_desc.Texture2D.PlaneSlice = 1;
			cpu_handle.ptr += descriptor_size;
			device->CreateShaderResourceView(displayed_image.texture.Get(), &srv_desc, cpu_handle); // Texture2D<float2> input_chrominance : register(t1);
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
			uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uav_desc.Format = swapchain_format;
			cpu_handle.ptr += descriptor_size;
			device->CreateUnorderedAccessView(swapchain_uav.Get(), nullptr, &uav_desc, cpu_handle); // RWTexture2D<unorm float4> output : register(u0);
			graphics_cmd->SetComputeRootDescriptorTable(1, descriptor_heap->GetGPUDescriptorHandleForHeapStart());
			graphics_cmd->SetComputeRoot32BitConstants(0, 2, &video.width, 0); // set video width and height
			graphics_cmd->Dispatch((swapchain_width + 7u) / 8u, (swapchain_height + 7u) / 8u, 1); // shader runs 8x8 threadgroup

			// reverse barriers
			std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
			std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
			std::swap(barriers[2].Transition.StateBefore, barriers[2].Transition.StateAfter);
			graphics_cmd->ResourceBarrier(arraysize(barriers), barriers);
		}

		// Workaround the fact that we cannot render into swapchain from compute shader in DX12, instead we copy into it:
		{
			ID3D12Resource* current_swapchain_resource = swapchain_resources[swapchain->GetCurrentBackBufferIndex()].Get();
			graphics_cmd->CopyResource(current_swapchain_resource, swapchain_uav.Get());
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = current_swapchain_resource;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			graphics_cmd->ResourceBarrier(1, &barrier);
		}

		hr = graphics_cmd->Close();
		assert(SUCCEEDED(hr));

		if (must_decode)
		{
			// wait for video queue completion on the GPU:
			hr = graphics_queue->Wait(video_fence.Get(), 1);
			assert(SUCCEEDED(hr));
		}

		ID3D12CommandList* commandlists[] = { graphics_cmd.Get() };
		graphics_queue->ExecuteCommandLists(arraysize(commandlists), commandlists);
		hr = graphics_queue->Signal(graphics_fence.Get(), 1);
		assert(SUCCEEDED(hr));

		hr = swapchain->Present(1, 0); // vsync
		assert(SUCCEEDED(hr));

		// In this sample, I always wait for GPU completion on the CPU to simplify command buffer and descriptor management:
		hr = graphics_fence->SetEventOnCompletion(1, nullptr);
		assert(SUCCEEDED(hr));
		hr = graphics_fence->Signal(0);
		assert(SUCCEEDED(hr));
		hr = video_fence->Signal(0);
		assert(SUCCEEDED(hr));
	}

	// Wait for GPU to become idle before exiting and ComPtrs are destroyed:
	ComPtr<ID3D12Fence> fence;
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));
	hr = graphics_queue->Signal(fence.Get(), 1);
	assert(SUCCEEDED(hr));
	hr = fence->SetEventOnCompletion(1, nullptr);
	assert(SUCCEEDED(hr));

	return 0;
}
