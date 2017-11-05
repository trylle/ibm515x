#ifndef CGA_DOWNSAMPLE_H
#define CGA_DOWNSAMPLE_H

#include <type_traits>

#include <boost/optional.hpp>

#include <eigen3/Eigen/Dense>

#include "netvid/framebuffer.h"

#include "common/cga.h"

#include "parallel_process.h"
#include "bayer.h"

// returns IRGB
extern std::uint8_t eval_nearest_color(const std::vector<std::array<float, 3>> &linear_palette, const std::array<float, 3> &linear_color, float *best_distance_out=nullptr);

struct dithered_color
{
	std::uint8_t left_color=0;
	std::uint8_t right_color=0;
	float mix=0;

	std::uint8_t get_dithered(const bayer::map &bm, int x, int y) const
	{
		return bm.is_on(x, y, mix) ? right_color : left_color;
	}
};

extern dithered_color eval_dither_mix(const std::vector<std::array<float, 3>> &linear_palette, const std::array<float, 3> &target_linear_color, int left_idx, int right_idx);

inline static bool allowed_dither(int left, int right)
{
	if (left==right)
		return true;

	if (right<left)
		std::swap(left, right);

	if (left==0)
		return (right>=1 && right<=6) || right==8; // black should only mix with dark colors

	if (left==8)
		return false; // light black can't mix with bright colors

	bool is_blue=(left==1 || right==1 || left==9 || right==9);
	bool is_green=(left==2 || right==2 || left==10 || right==10);
	bool is_cyan=(left==3 || right==3 || left==11 || right==11);
	bool is_red=(left==4 || right==4 || left==12 || right==12);
	bool is_magenta=(left==5 || right==5 || left==13 || right==13);
	bool is_yellow=(left==6 || right==6 || left==14 || right==14);

	if (is_blue && (is_green || is_yellow || is_red))
		return false;

	if (is_green && (is_blue || is_red || is_magenta))
		return false;

	if (is_cyan && (is_red || is_magenta || is_yellow))
		return false;

	if (is_red && (is_green || is_blue || is_cyan))
		return false;

	if (is_magenta && (is_green || is_cyan || is_yellow))
		return false;

	if (is_yellow && (is_blue || is_cyan || is_magenta))
		return false;

	if (left==4 && right==14)
		return false; // disallow dark red and bright yellow mix

	if ((left>=0 && left<=6) && right==15)
		return false; // disallow dark colors and white

	return true;
}

extern dithered_color eval_nearest_dithered_color(const std::vector<std::array<float, 3>> &linear_palette, const std::function<bool(int, int)> &allowed_dither, const std::array<float, 3> &linear_color, float *best_distance_out=nullptr);

struct dither_lut_t
{
	std::vector<std::array<float, 3>> linear_palette;
	std::vector<dithered_color> lookup;

	static constexpr auto pixel_fmt()
	{
		return fmt_r5g6b5;
	}

	dither_lut_t();
	dither_lut_t(const std::vector<std::array<float, 3>> &linear_palette, const std::function<dithered_color(const std::array<float, 3> &)> &dither_lookup);

	dithered_color get(const std::array<float, 3> &linear_color) const;
};

struct normal_output
{
	static void new_frame(const frame_data &in, frame_data_managed &out)
	{
		out.resize(in.width, in.height, 4);
		out.aspect_ratio=in.aspect_ratio;
	}

	static void pp(frame_data &out, int x, int y, std::uint8_t color)
	{
		auto &o=*out.pixel<std::uint8_t>(x, y);

		int shl=(x%2==1) ? 4 : 0;

		o&=~(((1 << 4)-1) << shl);
		o|=color << shl;
	}
};

struct temporal_dither_output
{
	std::vector<std::pair<int, int>> indices;
	int frame_count=0;
	bool staggered=false;

	void new_frame(const frame_data &in, frame_data_managed &out)
	{
		out.resize(in.width, in.height, 4);
		out.aspect_ratio=in.aspect_ratio;
		++frame_count;
	}

	void pp(frame_data &out, int x, int y, std::uint8_t color)
	{
		auto p=indices[color];

		if (staggered && (x%2)!=(y%2))
			std::swap(p.first, p.second);

		normal_output::pp(out, x, y, (frame_count%2==0) ? p.first : p.second);
	}
};

struct async_temporal_dither_output
{
	std::vector<std::pair<int, int>> indices;
	int frame_count=0;
	bool staggered=false;

	void new_frame(const frame_data &in, frame_data_managed &out)
	{
		out.resize(in.width, in.height, 8);
		out.aspect_ratio=in.aspect_ratio;
		++frame_count;
	}

	void pp(frame_data &out, int x, int y, std::uint8_t color)
	{
		const auto &p=indices[color];
		auto &o=*out.pixel<std::uint8_t>(x, y);

		if (!staggered || (x%2)==(y%2))
			o=(p.first << 4)+p.second;
		else
			o=(p.second << 4)+p.first;
	}
};

template<class output_algorithm_t=normal_output>
struct nearest
{
	output_algorithm_t output_algorithm;
	std::vector<std::array<float, 3>> linear_palette;

	static parallel_process::render_pass_t create(const std::vector<std::array<float, 3>> &linear_palette, const output_algorithm_t &output_algorithm=output_algorithm_t());

	void init(const frame_data &in, parallel_process::render_pass_t &render_pass);
	void render(const frame_data &in, frame_data &out, const render_context &ctx);
};

template<class output_algorithm_t=normal_output>
struct bayer_r
{
	output_algorithm_t output_algorithm;
	bayer::map bayer_map;
	dither_lut_t precomputed_dither;

	static parallel_process::render_pass_t create(const bayer::map &bayer_map, const dither_lut_t &precomputed_dither, const output_algorithm_t &output_algorithm=output_algorithm_t());

	void init(const frame_data &in, parallel_process::render_pass_t &render_pass);
	void render(const frame_data &in, frame_data &out, const render_context &ctx);
};

template<class output_algorithm_t=normal_output>
struct temporal_error_diffusion
{
	output_algorithm_t output_algorithm;
	frame_data_managed error;
	frame_data_managed prev_pixel;
	std::vector<std::array<float, 3>> linear_palette;

	static parallel_process::render_pass_t create(const std::vector<std::array<float, 3>> &linear_palette, const output_algorithm_t &output_algorithm=output_algorithm_t());

	void init(const frame_data &in, parallel_process::render_pass_t &render_pass);
	void render(const frame_data &in, frame_data &out, const render_context &ctx);
};

extern std::array<float, 3> srgb_from_image(const frame_data &in, int x, int y);
extern std::array<float, 3> local_contrast(const frame_data &img, int x, int y, float stddev, float gain);
extern parallel_process::render_pass_t linearize();
template<class storage_type>
extern parallel_process::render_pass_t unlinearize(const pixel_format<storage_type> &fmt);
extern parallel_process::render_pass_t nearest_scale(int w, int h);
extern parallel_process::render_pass_t black_crush(float black_crush_low=0, float black_crush_high=0.015f);
extern void add_local_contrast(std::vector<parallel_process::render_pass_t> &passes, float stddev, float gain, float black_crush_high=0.015f, float black_crush_low=0);

#endif /* CGA_DOWNSAMPLE_H */
