# Mini Video Sample
This repository shows how to use the Vulkan and DirectX 12 graphics API to decode H264 videos in a straightforward way, without hiding anything from you. This means no external dependencies used, everything that you need to get started is right here, with full source code from loading a file with raw C++, extracting the H264 bitstream from it, parsing the metadata, then sending it to the GPU for decoding and finally displaying it on the screen. The focus is to use the fastest path of GPU decoding, without any CPU intervention more than absolutely necessary. Because the whole decoding process is entirely handled by Vulkan or DirectX 12 API by using their native native resource types, this solution is the most optimal way of decoding a video in games which are already heavily using GPU resources in the same way. Applying videos in <a href = "https://youtu.be/c1y38w8BZKw?si=O21RdHJtLeHPpBbU">in the game world</a> straight from decoded resources becomes just as trivial as using any other texture. Furthermore, you get full access to async compute functionality with the harware video decoding queue to get the most out of parallel GPU execution (decoding a video while rendering unrelated things at the same time).

This program displays a simple video in a window, running continuously:

![screenshot](include/screenshot.png?raw=true "Screenshot")

Before building it yourself, you can try the provided mini_video_vulkan.exe or the mini_video_dx12.exe to check whether it works on your system.

How to build:
- the only file you need to compile is either mini_video_vulkan.cpp or mini_video_dx12.cpp, with the main function as the entry point
- when you run it, information starts printing in the console and if everything's successful, then a video will be shown in a window

How to a load different video:
- enter the video name as startup argument, for example: `video.mp4` (only mp4 can be opened with a startup argument)

Features:
- Opening MP4 files which contain H264 data with AVCC layout (done by the <a href = "https://github.com/lieff/minimp4">minimp4</a> header only library)
- Opening raw Annex-B style H264 bitstream
- DirectX 12 API support
- Vulkan API support

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
