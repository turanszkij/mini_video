# Mini Video Sample [![Github Build Status](https://github.com/turanszkij/mini_video/workflows/Build/badge.svg)](https://github.com/turanszkij/mini_video/actions)

This sample shows how to use the Vulkan, DirectX 12 and DirectX 11 graphics APIs to utilize the hardware video decoder.

A screenshot of this program:

![screenshot](include/screenshot.png?raw=true "Screenshot")

Why use GPU video decoding:

The focus is to use the fastest path of GPU decoding, without any more CPU usage than absolutely necessary. Because the whole decoding process is entirely handled by the graphics API by using their native resource types, this solution is the most optimal way of decoding a video in games which already use the same type of GPU resources. Applying videos <a href = "https://youtu.be/c1y38w8BZKw?si=O21RdHJtLeHPpBbU">in the game world</a> from decoded resources becomes just as trivial as using any other texture while still using the optimized video compression codecs, there is no need to interface with an other library. Furthermore, you get full access to async compute functionality with the video decoding hardware to get the most out of parallel GPU execution (decoding a video while rendering unrelated things at the same time). Check out <a href = "https://github.com/turanszkij/WickedEngine">Wicked Engine</a> for a full implementation of GPU video decoding in a game engine with fully leveraging async video decoding and using video textures for materials and lights in the 3D world.

Platform:
- Windows (using the Visual Studio compiler)
- Linux (using the G++ compiler)

How to build:
- Windows: Run `build_windows.bat`
- Linux: Run `build_linux.sh`

How to use:
- run `mini_video_vulkan.exe`, `mini_video_dx12.exe` or `mini_video_dx11.exe`, it will play `test.mp4` by default
- enter the video name as command line argument, for example: `video.mp4`

Features:
- Opening MP4 files which contain H264 data with AVCC layout
- Opening raw Annex-B style H264 bitstream
- Vulkan API with validation support when built in Debug mode (if `_DEBUG` is defined)
- DirectX 12 API with validation support when built in Debug mode (if `_DEBUG` is defined)
- DirectX 11 API with validation support when built in Debug mode (if `_DEBUG` is defined)

Limitations:
- Only H264 compression is supported currently
- Probably a lot of H264 features are untested
- Some videos can have frame pacing issues
- Differences in decoding results might occur with different GPUs
- Audio is not handled

Libraries used:
- <a href = "https://github.com/lieff/minimp4">minimp4</a> (to load H264 data from MP4 file)
- <a href = "https://github.com/turanszkij/mini_video/blob/master/include/h264.h">H264 parser</a> (to extract encoding metadata from H264 format)
- <a href = "https://github.com/KhronosGroup/Vulkan-Headers">Vulkan Headers</a> (to use Vulkan without installing the Vulkan SDK)
- <a href = "https://github.com/zeux/volk">volk</a> (to use Vulkan without installing the Vulkan SDK)
- <a href = "https://github.com/microsoft/DirectXShaderCompiler">DirectXShaderCompiler</a> (to compile HLSL shaders for Vulkan and DirectX 12)

Structure of the program:
- `include/common.h` contains the logical `Video` description structure and some other helpers
- `mini_video_vulkan.cpp` contains the Vulkan code and the `main()` function
- `mini_video_dx12.cpp` contains the DX12 code and the `main()` function
- `mini_video_dx11.cpp` contains the DX11 code and the `main()` function
- `yuv_to_rgbCS.hlsl` is a compute shader that converts the video from YUV to RGB and outputs to screen
- The `main()` function does roughly the same things in all cases, just expressed with a different APIs:
  - Create the device object which interfaces with the GPU
  - Creates the video decoder object and memory allocation for it
  - Creates GPU buffer and copies the whole H264 bitstream into it
  - Creates the DPB texture 2D array which is responsible to hold the decoding reference frames
  - Compiles the `include/yuv_to_rgbCS.hlsl` shader and creates compute pipeline state from it
  - Creates graphics and video command queues and command lists
  - Creates the window
  - Creates the swapchain for the window
  - runs an infinite loop until the application is closed and loops the video frames
  - waits for GPU completion and destroys resources

Possible future improvements:
- H265 decoding
- AV1 decoding
- HDR video

You can also read about my video decoding implementation in Wicked Engine (that this sample was based on) here: <a href = "https://wickedengine.net/2023/05/vulkan-video-decoding/">Wicked Engine Devblog</a>. 

License: MIT

Copyright (c) 2025 Turánszki János

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
