#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <cstring>

static_assert(sizeof(void*) == sizeof(uint64_t), "Error: only 64-bit build is supported!");

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"	// mp4 -> h264 extraction
#include "h264.h"		// h264 parsing

// Video management utility:
struct Video
{
	// Video data that doesn't change during decoding:
	std::vector<uint8_t> h264_data; // raw h264 data with NAL start codes 0,0,0,1 or 0,0,1 before every NAL unit section
	uint32_t width = 0; // display width
	uint32_t height = 0; // display height
	uint32_t padded_width = 0; // decoder's working width
	uint32_t padded_height = 0; // decoder's working height
	uint32_t num_dpb_slots = 0; // maximum number of texture array slices in the Decoded Picture Buffer (DPB)
	std::vector<h264::SPS> sps_array; // array of h264 Picture Parameter Sets, usually there is only one
	std::vector<h264::PPS> pps_array; // array of h264 Sequence Parameter Sets, usually there is only one
	std::vector<h264::SliceHeader> slice_headers; // array of h264 Slice Headers, there are as many of them as video frames
	float duration_seconds = 0; // whole video duration in seconds
	struct FrameInfo
	{
		uint64_t offset = 0;
		uint64_t size = 0;
		float timestamp_seconds = 0;
		float duration_seconds = 0;
		uint32_t reference_priority = 0;
		int poc = 0;
		int gop = 0;
		int display_order = 0;
		bool is_intra = false;
	};
	std::vector<FrameInfo> frame_infos; // information about every frame in the order they were encontered inside the h264 stream (so they are in decode order, not display order)

	// Decoder timing state:
	int frameIndex = 0; // currently decoding h264 slice
	float time_until_next_frame = 0; // this is used to track when we need to swap displayed images, if it reaches 0 then we swap to the next one that can be displayed
	struct Timer
	{
		std::chrono::high_resolution_clock::time_point timestamp = std::chrono::high_resolution_clock::now();
		inline void record() { timestamp = std::chrono::high_resolution_clock::now(); }
		inline float elapsed_seconds() { return std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::high_resolution_clock::now() - timestamp).count(); }
	} timer; // the timer is used to time the swapping of displayed pictures

	// Decoded Picture Buffer (DPB) state:
	int poc_status[17] = {}; // tracking the PictureOrderCount for every used DPB slot
	int framenum_status[17] = {}; // tracking the frame index for every used DPB slot
	std::vector<uint8_t> reference_usage; // tracking the state of reference frames
	uint8_t next_ref = 0;
	uint8_t next_slot = 0;
	uint8_t current_slot = 0;

	// File loading helpers:

	// Extract the H264 data from an MP4 video file using the minimp4 library:
	bool Load_mp4(const char* filename)
	{
		std::vector<uint8_t> filedata;
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (file.is_open())
		{
			size_t dataSize = (size_t)file.tellg();
			file.seekg((std::streampos)0);
			filedata.resize(dataSize);
			file.read((char*)filedata.data(), dataSize);
			file.close();
		}
		if (filedata.empty())
		{
			printf("File not found: %s\n", filename);
			return false;
		}

		bool success = false;
		const uint8_t* input_buf = filedata.data();
		struct INPUT_BUFFER
		{
			const uint8_t* buffer;
			size_t size;
		} input_buffer = { input_buf,filedata.size() };
		MP4D_demux_t mp4 = {};
		int token = 0;
		auto read_callback = [](int64_t offset, void* buffer, size_t size, void* token) -> int
			{
				INPUT_BUFFER* buf = (INPUT_BUFFER*)token;
				size_t to_copy = MINIMP4_MIN(size, buf->size - offset - size);
				std::memcpy(buffer, buf->buffer + offset, to_copy);
				return to_copy != size;
			};
		int result = MP4D_open(&mp4, read_callback, &input_buffer, (int64_t)filedata.size());
		if (result != 1)
		{
			printf("MP4 parsing failure! MP4D_open result = %d", result);
			return false;
		}

		uint64_t slice_size = 0;

		for (uint32_t ntrack = 0; ntrack < mp4.track_count; ntrack++)
		{
			const MP4D_track_t& track = mp4.track[ntrack];

			if (track.handler_type != MP4D_HANDLER_TYPE_VIDE || track.object_type_indication != MP4_OBJECT_TYPE_AVC)
				continue; // Only AVC video (H264) is supported in this sample

			// SPS:
			{
				int size = 0;
				int index = 0;
				const void* data = nullptr;
				while (data = MP4D_read_sps(&mp4, ntrack, index, &size))
				{
					const uint8_t* sps_data = (const uint8_t*)data;

					h264::Bitstream bs = {};
					bs.init(sps_data, size);
					h264::NALHeader nal = {};
					h264::read_nal_header(&nal, &bs);
					assert(nal.type == h264::NAL_UNIT_TYPE_SPS);
					
					sps_array.emplace_back();
					h264::SPS& sps = sps_array.back();
					h264::read_sps(&sps, &bs);

					// Some validation checks that data parsing returned expected values:
					//	https://stackoverflow.com/questions/6394874/fetching-the-dimensions-of-a-h264video-stream
					width = ((sps.pic_width_in_mbs_minus1 + 1) * 16) - sps.frame_crop_left_offset * 2 - sps.frame_crop_right_offset * 2;
					height = ((2 - sps.frame_mbs_only_flag) * (sps.pic_height_in_map_units_minus1 + 1) * 16) - (sps.frame_crop_top_offset * 2) - (sps.frame_crop_bottom_offset * 2);
					assert(track.SampleDescription.video.width == width);
					assert(track.SampleDescription.video.height == height);
					padded_width = (sps.pic_width_in_mbs_minus1 + 1) * 16;
					padded_height = (sps.pic_height_in_map_units_minus1 + 1) * 16;
					num_dpb_slots = std::max(num_dpb_slots, uint32_t(sps.num_ref_frames + 1));

					index++;
				}
			}

			// PPS:
			{
				int size = 0;
				int index = 0;
				const void* data = nullptr;
				while (data = MP4D_read_pps(&mp4, ntrack, index, &size))
				{
					const uint8_t* pps_data = (const uint8_t*)data;

					h264::Bitstream bs = {};
					bs.init(pps_data, size);
					h264::NALHeader nal = {};
					h264::read_nal_header(&nal, &bs);
					assert(nal.type == h264::NAL_UNIT_TYPE_PPS);

					pps_array.emplace_back();
					h264::PPS& pps = pps_array.back();
					h264::read_pps(&pps, &bs);

					index++;
				}
			}

			double timescale_rcp = 1.0 / double(track.timescale);

			uint32_t track_duration = 0;
			for (uint32_t i = 0; i < track.sample_count; i++)
			{
				unsigned frame_bytes, timestamp, duration;
				MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
				track_duration += duration;

				frame_infos.emplace_back();
				Video::FrameInfo& frame_info = frame_infos.back();

				const uint8_t* src_buffer = input_buf + ofs;
				while (frame_bytes > 0)
				{
					uint32_t size = ((uint32_t)src_buffer[0] << 24) | ((uint32_t)src_buffer[1] << 16) | ((uint32_t)src_buffer[2] << 8) | src_buffer[3];
					size += 4;
					assert(frame_bytes >= size);

					h264::Bitstream bs = {};
					bs.init(&src_buffer[4], frame_bytes);
					h264::NALHeader nal = {};
					h264::read_nal_header(&nal, &bs);

					if (nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR)
					{
						frame_info.is_intra = true;
					}
					else if (nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR)
					{
						frame_info.is_intra = false;
					}
					else
					{
						// Continue search for frame beginning NAL unit:
						frame_bytes -= size;
						src_buffer += size;
						continue;
					}

					slice_headers.emplace_back();
					h264::SliceHeader& slice_header = slice_headers.back();
					h264::read_slice_header(&slice_header, &nal, pps_array.data(), sps_array.data(), &bs);

					// Accept frame beginning NAL unit:
					frame_info.reference_priority = nal.idc;

					frame_info.offset = h264_data.size();
					frame_info.size = sizeof(h264::nal_start_code) + size - 4;
					h264_data.resize(h264_data.size() + frame_info.size);
					std::memcpy(h264_data.data() + frame_info.offset, h264::nal_start_code, sizeof(h264::nal_start_code));
					std::memcpy(h264_data.data() + frame_info.offset + sizeof(h264::nal_start_code), src_buffer + 4, size - 4);

					break;
				}

				slice_size += frame_info.size;
				frame_info.timestamp_seconds = float(double(timestamp) * timescale_rcp);
				frame_info.duration_seconds = float(double(duration) * timescale_rcp);
			}
		}
		MP4D_close(&mp4);
		CalculateFrameDisplayOrder();
		return true;
	}

	// Load from raw H264 data file (prefixed with 0,0,0,1 or 0,0,1 NAL unit start codes)
	bool Load_h264_raw(const char* filename, float framerate = 60.0f)
	{
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (file.is_open())
		{
			size_t dataSize = (size_t)file.tellg();
			file.seekg((std::streampos)0);
			h264_data.resize(dataSize);
			file.read((char*)h264_data.data(), dataSize);
			file.close();
		}
		if (h264_data.empty())
		{
			printf("File not found: %s\n", filename);
			return false;
		}

		h264::Bitstream bs;
		bs.init(h264_data.data(), h264_data.size());
		while (h264::find_next_nal(&bs))
		{
			const uint64_t nal_offset = bs.byte_offset() - sizeof(h264::nal_start_code);
			if (!frame_infos.empty())
			{
				// patch size of previous frame:
				Video::FrameInfo& frame_info = frame_infos.back();
				if (frame_info.size == 0)
				{
					frame_info.size = nal_offset - frame_info.offset;
				}
			}

			h264::NALHeader nal;
			if (h264::read_nal_header(&nal, &bs))
			{
				switch (nal.type)
				{
				case h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR:
				{
					frame_infos.emplace_back();
					Video::FrameInfo& frame_info = frame_infos.back();
					frame_info.offset = nal_offset;
					frame_info.is_intra = false;
					frame_info.reference_priority = nal.idc;
					slice_headers.emplace_back();
					h264::SliceHeader& slice_header = slice_headers.back();
					h264::read_slice_header(&slice_header, &nal, pps_array.data(), sps_array.data(), &bs);
				}
				break;
				case h264::NAL_UNIT_TYPE_CODED_SLICE_IDR:
				{
					frame_infos.emplace_back();
					Video::FrameInfo& frame_info = frame_infos.back();
					frame_info.offset = nal_offset;
					frame_info.is_intra = true;
					frame_info.reference_priority = nal.idc;
					slice_headers.emplace_back();
					h264::SliceHeader& slice_header = slice_headers.back();
					h264::read_slice_header(&slice_header, &nal, pps_array.data(), sps_array.data(), &bs);
				}
				break;
				case h264::NAL_UNIT_TYPE_SPS:
				{
					if (sps_array.empty()) // TODO: multiple SPS fix
					{
						sps_array.emplace_back();
						h264::SPS& sps = sps_array.back();
						h264::read_sps(&sps, &bs);

						width = ((sps.pic_width_in_mbs_minus1 + 1) * 16) - sps.frame_crop_left_offset * 2 - sps.frame_crop_right_offset * 2;
						height = ((2 - sps.frame_mbs_only_flag) * (sps.pic_height_in_map_units_minus1 + 1) * 16) - (sps.frame_crop_top_offset * 2) - (sps.frame_crop_bottom_offset * 2);
						padded_width = (sps.pic_width_in_mbs_minus1 + 1) * 16;
						padded_height = (sps.pic_height_in_map_units_minus1 + 1) * 16;
						num_dpb_slots = std::max(num_dpb_slots, uint32_t(sps.num_ref_frames + 1));
						printf("Resolution = %d x %d (padded: %d x %d)\nDPB slots: %d\n", (int)width, (int)height, (int)padded_width, (int)padded_height, (int)num_dpb_slots);
					}
				}
				break;
				case h264::NAL_UNIT_TYPE_PPS:
				{
					if (pps_array.empty()) // TODO: multiple PPS fix
					{
						pps_array.emplace_back();
						h264::PPS& pps = pps_array.back();
						h264::read_pps(&pps, &bs);
					}
				}
				break;
				default:
					break;
				}
			}
			else
			{
				printf("found invalid NAL unit!\n");
				assert(0);
			}
		}
		if (!frame_infos.empty())
		{
			// patch size of last frame:
			Video::FrameInfo& frame_info = frame_infos.back();
			if (frame_info.size == 0)
			{
				frame_info.size = bs.byte_offset() - frame_info.offset;
			}
		}

		const float frame_duration = 1.0f / framerate;
		duration_seconds = 0;
		for (Video::FrameInfo& frame_info : frame_infos)
		{
			frame_info.duration_seconds = frame_duration;
			frame_info.timestamp_seconds = duration_seconds;
			duration_seconds += frame_info.duration_seconds;
		}

		CalculateFrameDisplayOrder();
		return true;
	}

	// Calculate the frame reordering based on the H264 info:
	void CalculateFrameDisplayOrder()
	{
		int prev_pic_order_cnt_lsb = 0;
		int prev_pic_order_cnt_msb = 0;
		int poc_cycle = 0;
		for (size_t i = 0; i < frame_infos.size(); ++i)
		{
			const h264::SliceHeader& slice_header = slice_headers[i];
			const h264::PPS& pps = pps_array[slice_header.pic_parameter_set_id];
			const h264::SPS& sps = sps_array[pps.seq_parameter_set_id];

			// Rec. ITU-T H.264 (08/2021) page 77
			int max_pic_order_cnt_lsb = 1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
			int pic_order_cnt_lsb = slice_header.pic_order_cnt_lsb;

			if (pic_order_cnt_lsb == 0)
			{
				poc_cycle++;
			}

			// Rec. ITU-T H.264 (08/2021) page 115
			// Also: https://www.ramugedia.com/negative-pocs
			int pic_order_cnt_msb = 0;
			if (pic_order_cnt_lsb < prev_pic_order_cnt_lsb && (prev_pic_order_cnt_lsb - pic_order_cnt_lsb) >= max_pic_order_cnt_lsb / 2)
			{
				pic_order_cnt_msb = prev_pic_order_cnt_msb + max_pic_order_cnt_lsb; // pic_order_cnt_lsb wrapped around
			}
			else if (pic_order_cnt_lsb > prev_pic_order_cnt_lsb && (pic_order_cnt_lsb - prev_pic_order_cnt_lsb) > max_pic_order_cnt_lsb / 2)
			{
				pic_order_cnt_msb = prev_pic_order_cnt_msb - max_pic_order_cnt_lsb; // here negative POC might occur
			}
			else
			{
				pic_order_cnt_msb = prev_pic_order_cnt_msb;
			}
			//pic_order_cnt_msb = pic_order_cnt_msb % 256;
			prev_pic_order_cnt_lsb = pic_order_cnt_lsb;
			prev_pic_order_cnt_msb = pic_order_cnt_msb;

			// https://www.vcodex.com/h264avc-picture-management/
			frame_infos[i].poc = pic_order_cnt_msb + pic_order_cnt_lsb; // poc = TopFieldOrderCount
			frame_infos[i].gop = poc_cycle - 1;
		}

		std::vector<size_t> frame_display_order(frame_infos.size());
		for (size_t i = 0; i < frame_infos.size(); ++i)
		{
			frame_display_order[i] = i;
		}
		std::sort(frame_display_order.begin(), frame_display_order.end(), [&](size_t a, size_t b) {
			const Video::FrameInfo& frameA = frame_infos[a];
			const Video::FrameInfo& frameB = frame_infos[b];
			int64_t prioA = (int64_t(frameA.gop) << 32ll) | int64_t(frameA.poc);
			int64_t prioB = (int64_t(frameB.gop) << 32ll) | int64_t(frameB.poc);
			return prioA < prioB;
			});
		for (size_t i = 0; i < frame_display_order.size(); ++i)
		{
			frame_infos[frame_display_order[i]].display_order = (int)i;
		}
	}
};

// Other helpers:
#define arraysize(a) (sizeof(a) / sizeof(a[0]))

template<typename T>
constexpr T align(T value, T alignment)
{
	return ((value + alignment - T(1)) / alignment) * alignment;
}

#ifdef _WIN32
inline unsigned int firstbitlow(unsigned int value)
{
	unsigned long bit_index;
	if (_BitScanForward(&bit_index, value))
	{
		return (unsigned int)bit_index;
	}
	return 0;
}
#else
inline unsigned long firstbitlow(unsigned int value)
{
	if (value == 0)
	{
		return 0;
	}
	return __builtin_ctz(value);
}
#endif // _WIN32

// Windows specific things:
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#endif // _WIN32

// Linux specific things:
#ifdef __linux__
#include<X11/Xlib.h>
#include <dlfcn.h>
#define GetProcAddress(handle,name) dlsym(handle, name)
#define ComPtr CComPtr
#define __RPC_FAR
#endif // __linux__

// DirectX Shader Compiler library is used for Vulkan and DirectX shader compilation:
#include "dxcapi.h"

