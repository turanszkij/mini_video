#include "include/common.h"

#include <d3d11_3.h>
#include <dxgi1_3.h>
#include <initguid.h>
#include <dxva.h>
#include <d3dcompiler.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif // DEBUG

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
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
	ComPtr<IDXGIFactory2> dxgiFactory;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> immediate_context;
	ComPtr<ID3D11VideoDevice> video_device;
	ComPtr<ID3D11VideoContext> video_context;

	{
		UINT createDeviceFlags = 0;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif // _DEBUG

		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		uint32_t numFeatureLevels = arraysize(featureLevels);
		D3D_FEATURE_LEVEL featureLevel = {};

		hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevels, numFeatureLevels, D3D11_SDK_VERSION, &device, &featureLevel, &immediate_context);
		if (FAILED(hr))
		{
			printf("Failed to create DX11 device!\n");
			return -2;
		}

		hr = device.As(&video_device); 
		if (FAILED(hr))
		{
			printf("Failed to create DX11 Video device!\n");
			return -3;
		}

		hr = immediate_context.As(&video_context);
		if (FAILED(hr))
		{
			printf("Failed to create DX11 Video context!\n");
			return -4;
		}

		bool h264_supported = false;
		const UINT decoder_profile_count = video_device->GetVideoDecoderProfileCount();
		for (UINT i = 0; i < decoder_profile_count; ++i)
		{
			GUID profile = {};
			hr = video_device->GetVideoDecoderProfile(i, &profile);
			assert(SUCCEEDED(hr));
			if (profile == D3D11_DECODER_PROFILE_H264_VLD_NOFGT)
			{
				h264_supported = true;
				break;
			}
		}
		if (!h264_supported)
		{
			printf("DX11 Video device doesn't support the H264 decode profile!\n");
			return -3;
		}

		ComPtr<IDXGIDevice2> dxgiDevice;
		hr = device.As(&dxgiDevice);
		assert(SUCCEEDED(hr));

		hr = dxgiDevice->SetMaximumFrameLatency(1);
		assert(SUCCEEDED(hr));

		ComPtr<IDXGIAdapter> dxgiAdapter;
		hr = dxgiDevice->GetParent(IID_PPV_ARGS(&dxgiAdapter));
		assert(SUCCEEDED(hr));

		hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
		assert(SUCCEEDED(hr));

#ifdef _DEBUG
		ComPtr<ID3D11Debug> d3dDebug = nullptr;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&d3dDebug))))
		{
			ID3D11InfoQueue* d3dInfoQueue = nullptr;
			if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue)))
			{
				d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
			}
		}
#endif // _DEBUG
	}

	// Create decoder:
	ComPtr<ID3D11VideoDecoder> decoder;
	const DXGI_FORMAT decoder_format = DXGI_FORMAT_NV12;
	{
		D3D11_VIDEO_DECODER_DESC decoder_desc = {};
		decoder_desc.Guid = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
		decoder_desc.SampleWidth = video.padded_width;
		decoder_desc.SampleHeight = video.padded_height;
		decoder_desc.OutputFormat = decoder_format;
		UINT config_count = 0;
		hr = video_device->GetVideoDecoderConfigCount(&decoder_desc, &config_count);
		assert(SUCCEEDED(hr));
		D3D11_VIDEO_DECODER_CONFIG decoder_config = {};
		for (UINT i = 0; i < config_count; ++i)
		{
			hr = video_device->GetVideoDecoderConfig(&decoder_desc, i, &decoder_config);
			assert(SUCCEEDED(hr));
			if (
				decoder_config.guidConfigBitstreamEncryption == DXVA_NoEncrypt &&
				decoder_config.guidConfigMBcontrolEncryption == DXVA_NoEncrypt &&
				decoder_config.guidConfigResidDiffEncryption == DXVA_NoEncrypt
				)
			{
				break;
			}
		}
		hr = video_device->CreateVideoDecoder(&decoder_desc, &decoder_config, &decoder);
		assert(SUCCEEDED(hr));
	}

	// Create shader:
	ComPtr<ID3D11ComputeShader> compute_shader;
	{
		HMODULE d3dcompiler = LoadLibrary(L"d3dcompiler_47.dll");
		if (d3dcompiler != nullptr)
		{
			using PFN_D3DCOMPILE = decltype(&D3DCompile);
			PFN_D3DCOMPILE D3DCompile = (PFN_D3DCOMPILE)GetProcAddress(d3dcompiler, "D3DCompile");
			if (D3DCompile != nullptr)
			{
				std::vector<uint8_t> filedata;
				std::ifstream file("yuv_to_rgbCS.hlsl", std::ios::binary | std::ios::ate);
				if (file.is_open())
				{
					size_t dataSize = (size_t)file.tellg();
					file.seekg((std::streampos)0);
					filedata.resize(dataSize);
					file.read((char*)filedata.data(), dataSize);
					file.close();
				}
				ComPtr<ID3DBlob> code;
				ComPtr<ID3DBlob> errors;
				hr = D3DCompile(
					filedata.data(),
					filedata.size(),
					"yuv_to_rgbCS.hlsl",
					nullptr,
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					"main",
					"cs_5_0",
					0,
					0,
					&code,
					&errors
				);
				if (errors)
				{
					printf("Shader compile error: %s\n", (const char*)errors->GetBufferPointer());
					return -1;
				}

				if (SUCCEEDED(hr))
				{
					hr = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &compute_shader);
					assert(SUCCEEDED(hr));
				}
			}
		}
	}
	if (compute_shader == nullptr)
	{
		printf("Shader was not compiled!\n");
		return -1;
	}

	struct DecodeResultReordered
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11VideoDecoderOutputView> decoder_view;
		ComPtr<ID3D11ShaderResourceView> decoder_srv_luminance;
		ComPtr<ID3D11ShaderResourceView> decoder_srv_chrominance;
		int display_order = -1;
		int frame_index = 0;
		void create(ID3D11Device* device, ID3D11VideoDevice* video_device, uint32_t padded_width, uint32_t padded_height)
		{
			D3D11_TEXTURE2D_DESC texture_desc = {};
			texture_desc.Format = decoder_format;
			texture_desc.Width = padded_width;
			texture_desc.Height = padded_height;
			texture_desc.ArraySize = 1;
			texture_desc.SampleDesc.Count = 1;
			texture_desc.MipLevels = 1;
			texture_desc.Usage = D3D11_USAGE_DEFAULT;
			texture_desc.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
			HRESULT hr = device->CreateTexture2D(&texture_desc, nullptr, &texture);
			assert(SUCCEEDED(hr));

			D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC decoder_view_desc = {};
			decoder_view_desc.DecodeProfile = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
			decoder_view_desc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
			decoder_view_desc.Texture2D.ArraySlice = 0;
			hr = video_device->CreateVideoDecoderOutputView(texture.Get(), &decoder_view_desc, &decoder_view);
			assert(SUCCEEDED(hr));

			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv_desc.Texture2D.MipLevels = 1;

			srv_desc.Format = DXGI_FORMAT_R8_UNORM;
			hr = device->CreateShaderResourceView(texture.Get(), &srv_desc, &decoder_srv_luminance);
			assert(SUCCEEDED(hr));
			srv_desc.Format = DXGI_FORMAT_R8G8_UNORM;
			hr = device->CreateShaderResourceView(texture.Get(), &srv_desc, &decoder_srv_chrominance);
			assert(SUCCEEDED(hr));
		}
	};
	std::vector<DecodeResultReordered> reordered_results_free; // free image slots that can be reused as reordering image displaying
	std::vector<DecodeResultReordered> reordered_results_working; // image slots that are currently in use for reordering
	DecodeResultReordered displayed_image; // the latest reordered picture

	// Create constant buffer:
	ComPtr<ID3D11Buffer> constant_buffer;
	{
		D3D11_BUFFER_DESC buffer_desc = {};
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		buffer_desc.ByteWidth = sizeof(uint32_t) * 4;
		buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		hr = device->CreateBuffer(&buffer_desc, nullptr, &constant_buffer);
		assert(SUCCEEDED(hr));
	}

	// Create window to display video onto:
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
		wcex.lpszClassName = L"mini_video_dx11";
		wcex.hIconSm = NULL;
		RegisterClassExW(&wcex);
		hWnd = CreateWindowW(L"mini_video_dx11", L"mini_video_dx11", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, video.width, video.height, nullptr, nullptr, NULL, nullptr);
		ShowWindow(hWnd, SW_SHOWDEFAULT);
	}

	// Create swapchain for the window:
	ComPtr<IDXGISwapChain1> swapchain;
	ComPtr<ID3D11Texture2D> swapchain_texture;
	ComPtr<ID3D11UnorderedAccessView> swapchain_uav;
	UINT swapchain_width = 0;
	UINT swapchain_height = 0;
	DXGI_FORMAT swapchain_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	auto create_swapchain = [&]() { // because the swapchain will be recreated on window resize, I create a resusable lambda function for it

		RECT rect = {};
		GetClientRect(hWnd, &rect);
		swapchain_width = UINT(rect.right - rect.left);
		swapchain_height = UINT(rect.bottom - rect.top);

		if (swapchain == nullptr)
		{
			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChainDesc.Width = swapchain_width;
			swapChainDesc.Height = swapchain_height;
			swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.SampleDesc.Count = 1;
			swapChainDesc.BufferUsage = DXGI_USAGE_UNORDERED_ACCESS;
			swapChainDesc.BufferCount = 2;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			swapChainDesc.Flags = 0;
			swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc;
			fullscreenDesc.Windowed = TRUE;

			hr = dxgiFactory->CreateSwapChainForHwnd(
				device.Get(),
				hWnd,
				&swapChainDesc,
				&fullscreenDesc,
				nullptr,
				&swapchain
			);
			assert(SUCCEEDED(hr));
		}
		else
		{
			swapchain_texture.Reset();
			swapchain_uav.Reset();
			hr = swapchain->ResizeBuffers(2, swapchain_width, swapchain_height, swapchain_format, 0);
			assert(SUCCEEDED(hr));
		}

		hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&swapchain_texture));
		assert(SUCCEEDED(hr));

		hr = device->CreateUnorderedAccessView(swapchain_texture.Get(), nullptr, &swapchain_uav);
		assert(SUCCEEDED(hr));

		printf("swapchain resized, new size: %d x %d\n", (int)swapchain_width, (int)swapchain_height);
	};
	create_swapchain();

	// Create sampler:
	ComPtr<ID3D11SamplerState> sampler;
	{
		D3D11_SAMPLER_DESC sampler_desc = {};
		sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		hr = device->CreateSamplerState(&sampler_desc, &sampler);
		assert(SUCCEEDED(hr));
	}

	// Do the display frame loop:
	bool exiting = false;
	while (!exiting)
	{
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

		if (video.frameIndex == 0)
		{
			// At video beginning, reset the state of picture display reordering: 
			for (DecodeResultReordered& used : reordered_results_working)
			{
				used.display_order = -1;
				reordered_results_free.push_back(used);
			}
			reordered_results_working.clear();
			video.target_display_order = 0;
		}

		bool must_decode = true;
		for (auto& x : reordered_results_working)
		{
			if (x.display_order == video.target_display_order)
				must_decode = false;
		}

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

			// DirectX Video Acceleration for H.264/MPEG-4 AVC Decoding, Microsoft, Updated 2010, Page 31
			sliceinfo_h264.BSNALunitDataLocation = 0;
			sliceinfo_h264.SliceBytesInBuffer = (UINT)frame_info.size;
			sliceinfo_h264.wBadSliceChopping = 0; // whole slice is in the buffer


			// If decode happened this frame, then copy the latest output to the reordering picture queue:
			if (reordered_results_free.empty())
			{
				// Request new image, because there is no more free ones that we can use:
				reordered_results_free.emplace_back();
				reordered_results_free.back().create(device.Get(), video_device.Get(), video.padded_width, video.padded_height);
			}

			// Copy will be done from decoder output to the newly allocated reordered picture:
			DecodeResultReordered reordered_current = reordered_results_free.back();
			reordered_results_free.pop_back();
			reordered_current.display_order = video.frame_infos[video.frameIndex].display_order;
			reordered_current.frame_index = video.frameIndex;

			// Current latest reordered is pushed to the reorder working queue:
			reordered_results_working.push_back(reordered_current);

			hr = video_context->DecoderBeginFrame(decoder.Get(), reordered_current.decoder_view.Get(), 0, nullptr);
			assert(SUCCEEDED(hr));

			UINT buffer_size = 0;
			void* buffer = nullptr;

			hr = video_context->GetDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &buffer_size, &buffer);
			assert(SUCCEEDED(hr));
			assert(frame_info.size <= buffer_size);
			std::memcpy(buffer, video.h264_data.data() + frame_info.offset, frame_info.size);
			hr = video_context->ReleaseDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_BITSTREAM);
			assert(SUCCEEDED(hr));

			hr = video_context->GetDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &buffer_size, &buffer);
			assert(SUCCEEDED(hr));
			assert(sizeof(pic_params_h264) <= buffer_size);
			std::memcpy(buffer, &pic_params_h264, sizeof(pic_params_h264));
			hr = video_context->ReleaseDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS);
			assert(SUCCEEDED(hr));

			hr = video_context->GetDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, &buffer_size, &buffer);
			assert(SUCCEEDED(hr));
			assert(sizeof(qmatrix_h264) <= buffer_size);
			std::memcpy(buffer, &qmatrix_h264, sizeof(qmatrix_h264));
			hr = video_context->ReleaseDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX);
			assert(SUCCEEDED(hr));

			hr = video_context->GetDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &buffer_size, &buffer);
			assert(SUCCEEDED(hr));
			assert(sizeof(sliceinfo_h264) <= buffer_size);
			std::memcpy(buffer, &sliceinfo_h264, sizeof(sliceinfo_h264));
			hr = video_context->ReleaseDecoderBuffer(decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL);
			assert(SUCCEEDED(hr));

			D3D11_VIDEO_DECODER_BUFFER_DESC buffer_descs[4] = {};
			buffer_descs[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
			buffer_descs[0].DataSize = (UINT)frame_info.size;

			buffer_descs[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
			buffer_descs[1].DataSize = sizeof(pic_params_h264);

			buffer_descs[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
			buffer_descs[2].DataSize = sizeof(qmatrix_h264);

			buffer_descs[3].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
			buffer_descs[3].DataSize = sizeof(sliceinfo_h264);

			hr = video_context->SubmitDecoderBuffers(decoder.Get(), arraysize(buffer_descs), buffer_descs);
			assert(SUCCEEDED(hr));

			hr = video_context->DecoderEndFrame(decoder.Get());
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
			video.frameIndex = (video.frameIndex + 1) % video.frame_infos.size();
		}

		video.time_until_next_frame -= video.timer.elapsed_seconds();
		video.timer.record();
		if (video.time_until_next_frame <= 0)
		{
			// Search for the next displayable with lowest display order:
			int display_order_changed = -1;
			for (int i = 0; i < (int)reordered_results_working.size(); ++i)
			{
				if (reordered_results_working[i].display_order == video.target_display_order)
				{
					display_order_changed = i;
				}
			}
			if (display_order_changed >= 0)
			{
				// Free current output texture:
				if (displayed_image.texture != nullptr)
				{
					reordered_results_free.push_back(displayed_image);
				}
				// Take this used texture as current output:
				displayed_image = std::move(reordered_results_working[display_order_changed]);
				// Remove this used texture:
				std::swap(reordered_results_working[display_order_changed], reordered_results_working.back());
				reordered_results_working.pop_back();

				video.time_until_next_frame = video.frame_infos[displayed_image.frame_index].duration_seconds;

				assert(displayed_image.texture != nullptr);
				printf("\tDisplayed image changed, frame_index: %d, display_order: %d\n", displayed_image.frame_index, displayed_image.display_order);
				video.target_display_order++;
			}
		}

		// Bind shader resources and run the compute shader that resolves YUV to RGB:
		{
			immediate_context->CSSetShader(compute_shader.Get(), nullptr, 0);

			ID3D11ShaderResourceView* srvs[] = {
				displayed_image.decoder_srv_luminance.Get(),
				displayed_image.decoder_srv_chrominance.Get(),
			};
			immediate_context->CSSetShaderResources(0, arraysize(srvs), srvs);

			ID3D11UnorderedAccessView* uavs[] = { swapchain_uav.Get() };
			immediate_context->CSSetUnorderedAccessViews(0, arraysize(uavs), uavs, nullptr);

			ID3D11SamplerState* samplers[] = { sampler.Get() };
			immediate_context->CSSetSamplers(0, arraysize(samplers), samplers);

			immediate_context->UpdateSubresource(constant_buffer.Get(), 0, nullptr, &video.width, 0, 0);

			ID3D11Buffer* cbs[] = { constant_buffer.Get() };
			immediate_context->CSSetConstantBuffers(0, arraysize(cbs), cbs);

			immediate_context->Dispatch((swapchain_width + 7u) / 8u, (swapchain_height + 7u) / 8u, 1); // shader runs 8x8 threadgroup

			uavs[0] = nullptr;
			immediate_context->CSSetUnorderedAccessViews(0, arraysize(uavs), uavs, nullptr);
		}

		hr = swapchain->Present(1, 0);
		assert(SUCCEEDED(hr));
	}

	return 0;
}
