// This shader converts the video decoder output image which consists of two planes: Luminance, Chrominance into a regular RGB texture that can be displayed
//	It can be compiled for DX12 and Vulkan with DirectXShaderCompiler and for DX11 with D3DCompiler
//	Source: https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/shaders/yuv_to_rgbCS.hlsl

struct VideoConstants
{
	uint width;	 // without decoder padding
	uint height; // without decoder padding
};

#ifdef __hlsl_dx_compiler
// DirectX12, Vulkan:
[[vk::binding(0)]] SamplerState sampler_linear : register(s0);
[[vk::binding(1)]] Texture2D<float> input_luminance : register(t0);
[[vk::binding(2)]] Texture2D<float2> input_chrominance : register(t1);
[[vk::binding(3)]] [[vk::image_format("rgba8")]] RWTexture2D<unorm float4> output : register(u0);

#ifdef __spirv__
[[vk::push_constant]] VideoConstants video;
#else
ConstantBuffer<VideoConstants> video : register(b0);
#endif // __spriv__

#else
// DirectX 11:
SamplerState sampler_linear : register(s0);
Texture2D<float> input_luminance : register(t0);
Texture2D<float2> input_chrominance : register(t1);
RWTexture2D<unorm float4> output : register(u0);
cbuffer VIDEO_CB : register(b0)
{
	VideoConstants video;
}
#endif // __hlsl_dx_compiler

[RootSignature("RootConstants(num32BitConstants=2, b0), DescriptorTable(SRV(t0, numDescriptors = 2), UAV(u0)), StaticSampler(s0, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR)")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 padded_dim;
	input_luminance.GetDimensions(padded_dim.x, padded_dim.y);
	
	uint2 output_dim;
	output.GetDimensions(output_dim.x, output_dim.y);
	const float2 output_uv = float2((DTid.xy + 0.5f) / (float2)output_dim.xy);
	
	const float2 pixel_nonpadded = output_uv * float2(video.width, video.height);
	const float2 padded_uv = pixel_nonpadded / padded_dim; // this avoids sampling over the region which is padded and remains inside the original video resolution boundary

	float luminance = input_luminance.SampleLevel(sampler_linear, padded_uv, 0);
	float2 chrominance = input_chrominance.SampleLevel(sampler_linear, padded_uv, 0);

	// https://learn.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-8-bit-yuv-to-rgb888
	float C = luminance - 16.0 / 255.0;
	float D = chrominance.x - 0.5;
	float E = chrominance.y - 0.5;

	float r = saturate(1.164383 * C + 1.596027 * E);
	float g = saturate(1.164383 * C - (0.391762 * D) - (0.812968 * E));
	float b = saturate(1.164383 * C + 2.017232 * D);

	output[DTid.xy] = float4(r, g, b, 1);
}
