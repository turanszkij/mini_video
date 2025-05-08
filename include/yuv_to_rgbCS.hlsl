// This shader converts the video decoder output image which consists of two planes: Luminance, Chrominance into a regular RGB texture that can be displayed
//	It can be compiled for DX12 and Vulkan with DirectXShaderCompiler
//	Source: https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/shaders/yuv_to_rgbCS.hlsl
[[vk::binding(0)]] SamplerState sampler_linear : register(s0);
[[vk::binding(1)]] Texture2D<float> input_luminance : register(t0);
[[vk::binding(2)]] Texture2D<float2> input_chrominance : register(t1);
[[vk::binding(3)]] [[vk::image_format("rgba8")]] RWTexture2D<unorm float4> output : register(u0);

[RootSignature("DescriptorTable(SRV(t0, numDescriptors = 2), UAV(u0)), StaticSampler(s0, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR)")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 dim;
	output.GetDimensions(dim.x, dim.y);
	
	const float2 uv = float2((DTid.xy + 0.5f) / (float2)dim.xy);

	float luminance = input_luminance.SampleLevel(sampler_linear, uv, 0);
	float2 chrominance = input_chrominance.SampleLevel(sampler_linear, uv, 0);

	// https://learn.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-8-bit-yuv-to-rgb888
	float C = luminance - 16.0 / 255.0;
	float D = chrominance.x - 0.5;
	float E = chrominance.y - 0.5;

	float r = saturate(1.164383 * C + 1.596027 * E);
	float g = saturate(1.164383 * C - (0.391762 * D) - (0.812968 * E));
	float b = saturate(1.164383 * C + 2.017232 * D);

	output[DTid.xy] = float4(r, g, b, 1);
}
