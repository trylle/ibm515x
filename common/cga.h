#ifndef CGA_H
#define CGA_H

#include "netvid/framebuffer.h"

namespace framebuffer
{
	static const int bpp = 4; // RGBI
	static const int width = 320;
	static const int height = 200;
	static const int pitch = (width*bpp+7)/8;

	constexpr int bytes() { return height*pitch; }
}

namespace monitor_timing
{
	// adapted from http://www.paradigmlift.net/projects/teensy_cga.html
	static const int pixel_clock = 14318180;
	static const int framebuffer_pixel_width = 640/framebuffer::width; //!< framebuffer pixel width in native pixels
	static const int hscan_left_blanking = 56;
	static const int hscan_left_overscan = 40;
	static const int hscan_visible = 640;
	static const int hscan_right_overscan = 72;
	static const int hscan_right_blanking = 40;
	static const int hscan_sync = 64;
	static const int vscan_top_blanking = (239-228);
	static const int vscan_top_overscan = (261-239);
	static const int vscan_visible = 200;
	static const int vscan_bottom_overscan = (223-200);
	static const int vscan_bottom_blanking= (225-223);
	static const int vscan_sync = (228-225);

	constexpr int pixel_interval_ns(int pixels, int framebuffer_pixel_width = 1)
	{
		auto n = pixels*framebuffer_pixel_width*std::int64_t(1000*1000*1000);
		auto d = pixel_clock;

		return int(n/d);
	}
}

// based on https://en.wikipedia.org/wiki/Color_Graphics_Adapter#With_an_RGBI_monitor
inline std::vector<std::array<float, 3>> gen_cga_palette()
{
	std::vector<std::array<float, 3>> ret;
	const float major=2/3.f;
	const float minor=1/3.f;

	ret.reserve(16);

	for (int colorNumber=0; colorNumber<16; ++colorNumber)
	{
		ret.push_back(
		{
			major*(colorNumber & 4)/4 + minor*(colorNumber & 8)/8,
			major*(colorNumber & 2)/2 + minor*(colorNumber & 8)/8,
			major*(colorNumber & 1)/1 + minor*(colorNumber & 8)/8
		});

		if (colorNumber==6)
			ret.back()[1]/=2;
	}

	return ret;
}

inline std::tuple<std::vector<std::array<float, 3>>, std::vector<std::pair<int, int>>> combine_palette(const std::vector<std::array<float, 3>> &in_palette)
{
	std::vector<std::array<float, 3>> ret;
	std::vector<std::pair<int, int>> indices;

	std::size_t num=in_palette.size()*(in_palette.size()-1)/2;

	ret.reserve(num);
	indices.reserve(num);

	for (int i=0; i<int(in_palette.size()); ++i)
	{
		for (int j=i; j<int(in_palette.size()); ++j)
		{
			ret.push_back(lerp(in_palette[i], in_palette[j], .5f));
			indices.push_back(std::make_pair(i, j));
		}
	}

	return std::make_tuple(ret, indices);
}

inline std::vector<std::array<float, 3>> to_linear(const std::vector<std::array<float, 3>> &palette)
{
	std::vector<std::array<float, 3>> ret=palette;

	std::transform(ret.begin(), ret.end(), ret.begin(), [] (const auto &i) -> auto { return to_linear(i); });

	return ret;
}

inline static const std::vector<std::array<float, 3>> &cga_palette()
{
	static auto p=to_linear(gen_cga_palette());

	return p;
}

#endif /* CGA_H */
