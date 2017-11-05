#include "cga_downsample.h"

#include <thread>

#include "hsp.h"

std::uint8_t eval_nearest_color(const std::vector<std::array<float, 3>> &linear_palette, const std::array<float, 3> &linear_color, float *best_distance_out/*=nullptr*/)
{
	float best_distance=std::numeric_limits<float>::max();
	auto best_it=linear_palette.end();

	for (auto i=linear_palette.begin(); i!=linear_palette.end(); ++i)
	{
		const auto &candidate_color=*i;
		auto dist=distance(linear_color, candidate_color);

		if (dist>=best_distance)
			continue;

		best_it=i;
		best_distance=dist;
	}

	if (best_distance_out)
		*best_distance_out=best_distance;

	return std::distance(linear_palette.begin(), best_it);
}

float eval_dither_mix(const Eigen::Vector3f &target_color, const Eigen::Vector3f &left_color, const Eigen::Vector3f &right_color)
{
	using Eigen::Vector3f;

	Vector3f target_from_left=target_color-left_color;
	Vector3f delta=right_color-left_color;
	auto delta_len=delta.norm();
	Vector3f dir=delta/delta_len;
	auto t=target_from_left.dot(dir);

	return std::max(0.f, std::min(1.f, t/delta_len));
}

float eval_dither_mix(const std::array<float, 3> &target_color, const std::array<float, 3> &left_color, const std::array<float, 3> &right_color)
{
	using Eigen::Vector3f;

	return eval_dither_mix(Vector3f(target_color.data()), Vector3f(left_color.data()), Vector3f(right_color.data()));
}

dithered_color eval_nearest_dithered_color(const std::vector<std::array<float, 3>> &linear_palette, const std::function<bool(int, int)> &allowed_dither, const std::array<float, 3> &linear_color, float *best_distance_out)
{
	using Eigen::Vector3f;

	float best_distance=std::numeric_limits<float>::max();
	dithered_color best;
	Vector3f target(linear_color.data());

	// Test solid colors first
	for (std::size_t i=0; i<linear_palette.size(); ++i)
	{
		const auto &left_color=linear_palette[i];
		Vector3f left(left_color.data());
		auto distance=(target-left).norm();

		if (distance>=best_distance)
			continue;

		best_distance=distance;
		best={ std::uint8_t(i), std::uint8_t(i), 0 };
	}

	for (std::size_t i=0; i<linear_palette.size(); ++i)
	{
		const auto &left_color=linear_palette[i];
		Vector3f left(left_color.data());

		for (std::size_t j=i+1; j<linear_palette.size(); ++j)
		{
			if (!allowed_dither(i, j))
				continue;

			const auto &right_color=linear_palette[j];
			Vector3f right(right_color.data());
			auto mix_level=eval_dither_mix(target, left, right);
			auto mix_point=left+(right-left)*mix_level;
			auto distance=(target-mix_point).norm();

			if (distance>=best_distance)
				continue;

			best_distance=distance;
			best={ std::uint8_t(i), std::uint8_t(j), mix_level };
		}
	}

	if (best_distance_out)
		*best_distance_out=best_distance;

	return best;
}

dither_lut_t::dither_lut_t()
{

}

dither_lut_t::dither_lut_t(const std::vector<std::array<float, 3>> &linear_palette, const std::function<dithered_color(const std::array<float, 3> &)> &dither_lookup)
	: linear_palette(linear_palette)
{
	lookup.resize(1 << pixel_fmt().visible_bits());

	boost::asio::io_service io_service;
	std::vector<std::thread> threads;
	int thread_count=std::thread::hardware_concurrency();

	threads.reserve(thread_count);

	for (int i=0; i<thread_count; ++i)
	{
		int begin_row=(lookup.size()*i)/thread_count;
		int end_row=(lookup.size()*(i+1))/thread_count;

		io_service.post([&, begin_row, end_row]
		{
			for (int r=begin_row; r<end_row; ++r)
			{
				auto l=to_linear(to_float_srgb(pixel_fmt(), r));

				lookup[r]=dither_lookup(l);
			}
		});
	}

	for (int i=0; i<thread_count; ++i)
		threads.emplace_back([&] { io_service.run(); });

	for (auto &t : threads)
		t.join();
}

dithered_color dither_lut_t::get(const std::array<float, 3> &linear_color) const
{
	auto c=from_float_srgb(pixel_fmt(), to_srgb(linear_color));
	auto result=lookup[c];

	result.mix=eval_dither_mix(linear_color, linear_palette[result.left_color], linear_palette[result.right_color]);

	return result;
}

float gaussian_kernel(float x, float stddev)
{
	float s2=2*stddev*stddev;
	float a=(x*x)/s2;
	float w=expf(-a);

	return w;
}

template<class out_type, class init_t, class func_t>
out_type weighted_sample(const frame_data &img, int x, int y, float stddev, const init_t &init, const func_t &func)
{
	using Eigen::Vector2i;

	int radius=(int)ceil(stddev*6);
	out_type data;
	float weights=0;
	Vector2i pos;
	Vector2i top_left;
	Vector2i bottom_right;
	Vector2i frame_size;

	init(data);

	pos << x, y;
	frame_size << img.width, img.height;

	top_left=pos.unaryExpr([&] (int v) { return v-radius; });
	bottom_right=pos.unaryExpr([&] (int v) { return v+radius; });

	top_left=top_left.cwiseMax(0).cwiseMin(frame_size);
	bottom_right=bottom_right.cwiseMax(0).cwiseMin(frame_size);

	for (int y=top_left[1]; y<bottom_right[1]; ++y)
	{
		for (int x=top_left[0]; x<bottom_right[0]; ++x)
		{
			Vector2i sample_pos;

			sample_pos << x, y;

			auto sample_offset=(sample_pos-pos).eval();
			float l=sqrt(sample_offset.squaredNorm());
			float weight=gaussian_kernel(l, stddev);

			data+=func(sample_pos[0], sample_pos[1])*weight;
			weights+=weight;
		}
	}

	if (weights==0)
		return out_type();

	return data/weights;
}

struct math_array
{
	static void init(std::array<float, 2> &v)
	{
		v.fill(0);
	}

	static std::array<float, 2> mul(const std::array<float, 2> &x, float s)
	{
		return ::mul(x, s);
	}

	static std::array<float, 2> add(const std::array<float, 2> &left, const std::array<float, 2> &right)
	{
		return ::add(left, right);
	}
};

struct weighted_sample_1d_t
{
	std::vector<float> kernel;
	float stddev=0;
	int frame_width=0;
	int frame_height=0;

	void init_kernel(float stddev)
	{
		if (this->stddev==stddev)
			return;

		auto radius=stddev*6;
		int kernel_sizei=(int)ceil(radius)*2+1;

		kernel.resize(kernel_sizei);

		for (int i=0; i<kernel_sizei; ++i)
			kernel[i]=gaussian_kernel(i-kernel_sizei/2, stddev);

		this->stddev=stddev;
	}

	template<class out_type, class math_t, class func_t>
	out_type operator()(int x, int y, bool horizontal, const math_t &math, const func_t &func) const
	{
		out_type data;
		float weights=0;

		math.init(data);

		int mni=0;
		int mxi=kernel.size();
		const int &ud=horizontal ? x : y;
		const int max_d=horizontal ? frame_width : frame_height;

		mni=std::max<int>(mni, kernel.size()/2-ud);
		mxi=std::max<int>(mxi, kernel.size()/2-ud);

		mni=std::min<int>(mni, max_d+kernel.size()/2-ud);
		mxi=std::min<int>(mxi, max_d+kernel.size()/2-ud);

		int ux=x;
		int uy=y;

		for (int i=mni; i<mxi; ++i)
		{
			int &d=horizontal ? ux : uy;

			d=ud+i-kernel.size()/2;

			float weight=kernel[i];

			data=math.add(data, math.mul(func(ux, uy), weight));
			weights+=weight;
		}

		if (weights==0)
			return out_type();

		return math.mul(data, 1/weights);		
	}
};

template<class out_type, class math_t, class func_t>
out_type weighted_sample_1d(const frame_data &img, int x, int y, bool horizontal, float stddev, const math_t &math, const func_t &func)
{
	using Eigen::Vector2i;
	int radius=(int)ceil(stddev*6);
	out_type data;
	float weights=0;
	Vector2i pos;
	Vector2i top_left;
	Vector2i bottom_right;
	Vector2i frame_size;

	math.init(data);

	pos << x, y;
	frame_size << img.width, img.height;

	bottom_right=top_left=pos;

	if (horizontal)
	{
		top_left[0]-=radius;
		bottom_right[0]+=radius;
		bottom_right[1]++;
	}
	else
	{
		top_left[1]-=radius;
		bottom_right[1]+=radius;
		bottom_right[0]++;
	}

	top_left=top_left.cwiseMax(0).cwiseMin(frame_size);
	bottom_right=bottom_right.cwiseMax(0).cwiseMin(frame_size);

	for (int y=top_left[1]; y<bottom_right[1]; ++y)
	{
		for (int x=top_left[0]; x<bottom_right[0]; ++x)
		{
			Vector2i sample_pos;

			sample_pos << x, y;

			auto sample_offset=(sample_pos-pos).eval();
			float l=sqrt(sample_offset.squaredNorm());
			float weight=gaussian_kernel(l, stddev);

			//data+=math.mul(func(sample_pos[0], sample_pos[1]), weight);
			data=math.add(data, math.mul(func(sample_pos[0], sample_pos[1]), weight));
			weights+=weight;
		}
	}

	if (weights==0)
		return out_type();

	return math.mul(data, 1/weights);
}

std::array<float, 3> srgb_from_image(const frame_data &in, int x, int y)
{
	std::array<float, 3> srgb;

	if (in.bpp==16)
	{
		auto in_pixel=*in.pixel<std::uint16_t>(x, y);

		srgb=to_float_srgb(fmt_r5g6b5, in_pixel);
	}
	else if (in.bpp==32)
	{
		auto in_pixel=*in.pixel<std::uint32_t>(x, y);

		srgb=to_float_srgb(fmt_a8r8g8b8, in_pixel);
	}

	return srgb;
}

// adapted from https://en.wikipedia.org/wiki/Smoothstep
float clamp(float x, float lowerlimit, float upperlimit)
{
    if (x < lowerlimit) x = lowerlimit;
    if (x > upperlimit) x = upperlimit;
    return x;
}

// adapted from https://en.wikipedia.org/wiki/Smoothstep
float smootherstep(float edge0, float edge1, float x)
{
    // Scale, and clamp x to 0..1 range
    x = clamp((x - edge0)/(edge1 - edge0), 0.0, 1.0);
    // Evaluate polynomial
    return x*x*x*(x*(x*6 - 15) + 10);
}

std::array<float, 3> calc_local_contrast(float avg, float var, const std::array<float, 3> &linear_color, float gain, float black_crush_high=0.015f, float black_crush_low=0)
{
	float stddev=sqrt(var);
	float stddev_multiplier=1/3.f;

	float half_interval=stddev_multiplier*stddev;

	half_interval=std::max(half_interval, 3e-3f);

	float minimum=avg-half_interval;
	float maximum=avg+half_interval;

	minimum*=smootherstep(0.1f, .25f, minimum);
	minimum=0;

	auto hsp=rgb_to_hsp(linear_color);
	float newval=(hsp[2]-minimum)/(maximum-minimum);

	hsp[2]=hsp[2]+(newval-hsp[2])*gain;

	std::array<float, 3> ret=hsp_to_rgb(clamp(hsp));

	ret=clamp(ret);

	return ret;
}

std::array<float, 3> local_contrast(const frame_data &img, int x, int y, float stddev, float gain)
{
	auto init=[] (auto &v) { v = v.Zero(); };
	auto sampler=[&] (int x, int y)
	{
		auto linear_color=to_linear(srgb_from_image(img, x, y));
		auto hsp=rgb_to_hsp(linear_color);
		Eigen::Vector2f result;

		result << hsp[2], hsp[2]*hsp[2];

		return result;
	};

	auto result = weighted_sample<Eigen::Vector2f>(img, x, y, stddev, init, sampler);
	auto avg = result[0];
	auto var = result[1]-result[0]*result[0];
	auto linear_color=to_linear(srgb_from_image(img, x, y));

	return calc_local_contrast(avg, var, linear_color, gain);
}

parallel_process::render_pass_t linearize()
{
	return
	{
		[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, sizeof(float)*3*8);
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx)
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto &o=*out.pixel<std::array<float, 3>>(x, y);

					o=to_linear(srgb_from_image(in, x, y));
				}
			}
		}
	};
}

template<class storage_type>
parallel_process::render_pass_t unlinearize(const pixel_format<storage_type> &fmt)
{
	return
	{
		[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, fmt.bits());
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx)
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto &i=*in.pixel<std::array<float, 3>>(x, y);
					auto &o=*out.pixel<storage_type>(x, y);

					o=from_float_srgb(fmt, to_srgb(i));
				}
			}
		}
	};
}

template
parallel_process::render_pass_t unlinearize<std::uint32_t>(const pixel_format<std::uint32_t> &fmt);

template
parallel_process::render_pass_t unlinearize<std::uint16_t>(const pixel_format<std::uint16_t> &fmt);

void lc_blur(std::vector<parallel_process::render_pass_t> &render_passes, float stddev, const std::shared_ptr<frame_data_managed> &dest=nullptr)
{
	auto blur_pre=std::make_shared<frame_data_managed>();
	auto blur_x=std::make_shared<frame_data_managed>();
	weighted_sample_1d_t ws;
	auto ws_horizontal=std::make_shared<weighted_sample_1d_t>();

	ws.init_kernel(stddev);
	ws_horizontal->init_kernel(stddev);

	auto sampler_pre=[&] (const frame_data &img, int x, int y) -> std::array<float, 2>
	{
		auto linear_color=*img.pixel<std::array<float, 3>>(x, y);
		auto hsp=rgb_to_hsp(linear_color);

		return  { hsp[2], hsp[2]*hsp[2] };
	};

	auto sampler=[&] (const frame_data &img, int x, int y) -> std::array<float, 2>
	{
		return *img.pixel<std::array<float, 2>>(x, y);
	};

	render_passes.emplace_back(
		[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, in.pitch, in.bpp);
			render_pass.no_output=true;
			blur_pre->resize(in.width, in.height, sizeof(float)*2*8);
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx)
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto &o=*blur_pre->pixel<std::array<float, 2>>(x, y);

					o=sampler_pre(in, x, y);
				}
			}
		});

	render_passes.emplace_back(
		[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, in.pitch, in.bpp);
			render_pass.no_output=true;
			blur_x->resize(in.width, in.height, sizeof(float)*2*8);
			ws_horizontal->frame_width=in.width;
			ws_horizontal->frame_height=in.height;
			ws_horizontal->init_kernel(stddev*in.width/(in.height*in.aspect_ratio));
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx) mutable
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto &o=*blur_x->pixel<std::array<float, 2>>(x, y);

					o=ws_horizontal->operator()<std::array<float, 2>>(x, y, true, math_array(), [&] (int x, int y) { return sampler(*blur_pre, x, y); });
				}
			}
		});

	render_passes.emplace_back(
		[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, in.pitch, sizeof(float)*2*8);

			if (dest)
			{
				render_pass.no_output=true;
				dest->resize(in.width, in.height, sizeof(float)*2*8);
			}
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx) mutable
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			ws.frame_width=in.width;
			ws.frame_height=in.height;

			auto &current_dest=dest ? *dest : out;

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto &o=*current_dest.pixel<std::array<float, 2>>(x, y);

					o=ws.operator()<std::array<float, 2>>(x, y, false, math_array(), [&] (int x, int y) { return sampler(*blur_x, x, y); });
				}
			}
		});
}

parallel_process::render_pass_t black_crush(float black_crush_low, float black_crush_high)
{
	return
	{
		[] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, sizeof(float)*3*8);
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx)
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto linear_color=*in.pixel<std::array<float, 3>>(x, y);
					auto hsp=rgb_to_hsp(linear_color);

					hsp[2]*=smootherstep(black_crush_low, black_crush_high, hsp[2]);

					auto &o=*out.pixel<std::array<float, 3>>(x, y);

					o=hsp_to_rgb(hsp);
				}
			}
		}
	};
}


void add_local_contrast(std::vector<parallel_process::render_pass_t> &render_passes, float stddev, float gain, float black_crush_high, float black_crush_low)
{
	auto blur=std::make_shared<frame_data_managed>();

	lc_blur(render_passes, stddev, blur);

	render_passes.emplace_back(
		[] (const frame_data &in, parallel_process::render_pass_t &render_pass)
		{
			render_pass.frame.resize(in.width, in.height, sizeof(float)*3*8);
		},
		[=] (const frame_data &in, frame_data &out, const render_context &ctx)
		{
			int line_start, line_end;

			std::tie(line_start, line_end)=ctx.rows(in.height);

			for (int y=line_start; y<line_end; ++y)
			{
				for (int x=0; x<in.width; ++x)
				{
					auto avg_var=*blur->pixel<std::array<float, 2>>(x, y);
					auto avg=avg_var[0];
					auto var=avg_var[1]-avg*avg;
					auto linear_color=*in.pixel<std::array<float, 3>>(x, y);
					auto hsp=rgb_to_hsp(linear_color);

					var=std::max(var, 0.f);
					hsp[1]=pow(hsp[1], 0.75f);

					linear_color=hsp_to_rgb(hsp);

					auto c=calc_local_contrast(avg, var, linear_color, gain, black_crush_high, black_crush_low);
					auto &o=*out.pixel<std::array<float, 3>>(x, y);

					o=c;
				}
			}
		});
}

parallel_process::render_pass_t nearest_scale(int w, int h)
{
	return
		{
			[=](const frame_data &in, parallel_process::render_pass_t &render_pass)
			{
				render_pass.frame.resize(in.width*w, in.height*h, in.bpp);
			},
			[=](const frame_data &in, frame_data &out, const render_context &ctx)
			{
				int line_start, line_end;

				std::tie(line_start, line_end)=ctx.rows(in.height);

				for (int y=line_start; y<line_end; ++y)
				{
					for (int x=0; x<in.width; ++x)
					{
						auto linear_color=*in.pixel<std::array<float, 3>>(x, y);

						for (int j=0; j<h; ++j)
						{
							for (int i=0; i<w; ++i)
							{
								auto &o=*out.pixel<std::array<float, 3>>(x*w+i, y*h+j);

								o=linear_color;
							}
						}
					}
				}
			}
		};
}

template<class output_algorithm_t>
parallel_process::render_pass_t nearest<output_algorithm_t>::create(const std::vector<std::array<float, 3>> &linear_palette, const output_algorithm_t &output_algorithm)
{
	nearest<output_algorithm_t> n;

	n.linear_palette=linear_palette;
	n.output_algorithm=output_algorithm;

	return
	{
		[n] (auto &&...args) mutable
		{
			return n.init(std::forward<decltype(args)>(args)...);
		}
	};
}

template<class output_algorithm_t>
void nearest<output_algorithm_t>::init(const frame_data &in, parallel_process::render_pass_t &render_pass)
{
	output_algorithm.new_frame(in, render_pass.frame);
	render_pass.render=[this] (auto &&...args) { this->render(std::forward<decltype(args)>(args)...); };
}

template<class output_algorithm_t>
void nearest<output_algorithm_t>::render(const frame_data &in, frame_data &out, const render_context &ctx)
{
	int line_start, line_end;

	std::tie(line_start, line_end)=ctx.rows(in.height);

	for (int y=line_start; y<line_end; ++y)
	{
		for (int x=0; x<in.width; ++x)
		{
			const std::array<float, 3> &linear_color=*in.pixel<std::array<float, 3>>(x, y);
			std::uint8_t c=eval_nearest_color(linear_palette, linear_color);

			output_algorithm.pp(out, x, y, c);
		}
	}
}

template<class output_algorithm_t>
parallel_process::render_pass_t bayer_r<output_algorithm_t>::create(const bayer::map &bayer_map, const dither_lut_t &precomputed_dither, const output_algorithm_t &output_algorithm)
{
	bayer_r n;

	n.bayer_map=bayer_map;
	n.precomputed_dither=precomputed_dither;
	n.output_algorithm=output_algorithm;

	return
	{
		[n] (auto &&...args) mutable
		{
			return n.init(std::forward<decltype(args)>(args)...);
		}
	};
}

template<class output_algorithm_t>
void bayer_r<output_algorithm_t>::init(const frame_data &in, parallel_process::render_pass_t &render_pass)
{
	output_algorithm.new_frame(in, render_pass.frame);
	render_pass.render=[this] (auto &&...args)
	{
		return this->render(std::forward<decltype(args)>(args)...);
	};
}

template<class output_algorithm_t>
void bayer_r<output_algorithm_t>::render(const frame_data &in, frame_data &out, const render_context &ctx)
{
	int line_start, line_end;

	std::tie(line_start, line_end)=ctx.rows(in.height);

	for (int y=line_start; y<line_end; ++y)
	{
		for (int x=0; x<in.width; ++x)
		{
			const std::array<float, 3> &linear_color=*in.pixel<std::array<float, 3>>(x, y);
			auto dc=precomputed_dither.get(linear_color);
			std::uint8_t c=dc.get_dithered(bayer_map, x, y);

			output_algorithm.pp(out, x, y, c);
		}
	}
}

template<class output_algorithm_t>
parallel_process::render_pass_t temporal_error_diffusion<output_algorithm_t>::create(const std::vector<std::array<float, 3>> &linear_palette, const output_algorithm_t &output_algorithm)
{
	auto n=std::make_shared<temporal_error_diffusion>();

	n->linear_palette=linear_palette;
	n->output_algorithm=output_algorithm;

	return
	{
		[n] (auto &&...args)
		{
			return n->init(std::forward<decltype(args)>(args)...);
		}
	};
}

template<class output_algorithm_t>
void temporal_error_diffusion<output_algorithm_t>::init(const frame_data &in, parallel_process::render_pass_t &render_pass)
{
	output_algorithm.new_frame(in, render_pass.frame);
	error.resize(in.width, in.height, sizeof(std::array<float, 3>)*8);
	prev_pixel.resize(in.width, in.height, sizeof(std::array<float, 3>)*8);
	render_pass.render=[this] (auto &&...args)
	{
		return this->render(std::forward<decltype(args)>(args)...);
	};
}

template<class output_algorithm_t>
void temporal_error_diffusion<output_algorithm_t>::render(const frame_data &in, frame_data &out, const render_context &ctx)
{
	int line_start, line_end;

	std::tie(line_start, line_end)=ctx.rows(in.height);

	for (int y=line_start; y<line_end; ++y)
	{
		for (int x=0; x<in.width; ++x)
		{
			const std::array<float, 3> &linear_color=*in.pixel<std::array<float, 3>>(x, y);
			auto &linear_error=*error.pixel<std::array<float, 3>>(x, y);
			auto &prev=*prev_pixel.pixel<std::array<float, 3>>(x, y);
			auto cga_idx=eval_nearest_color(linear_palette, clamp(add(linear_color, linear_error)));

			output_algorithm.pp(out, x, y, cga_idx);

			auto current_error=sub(linear_color, cga_palette()[cga_idx]);

			add_ref(linear_error, current_error);

			if (prev!=linear_color)
			{
				for (int i=0; i<3; ++i)
				{
					float v=rand()/float(RAND_MAX);

					linear_error[i]+=v*current_error[i];
				}

				prev=linear_color;
			}

			clamp_ref(linear_error);
		}
	}
}

template
struct nearest<normal_output>;

template
struct nearest<temporal_dither_output>;

template
struct nearest<async_temporal_dither_output>;

template
struct bayer_r<normal_output>;

template
struct bayer_r<temporal_dither_output>;

template
struct bayer_r<async_temporal_dither_output>;

template
struct temporal_error_diffusion<normal_output>;

template
struct temporal_error_diffusion<temporal_dither_output>;

template
struct temporal_error_diffusion<async_temporal_dither_output>;
