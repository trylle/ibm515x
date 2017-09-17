/**
 * Linux framebuffer CGA renderer
 */

#include <iostream>

#ifndef __linux__
int main()
{
	std::cerr << "Unsupported platform" << std::endl;

	return 0;
}
#else
#include <numeric>
#include <chrono>

#include <boost/program_options.hpp>

#include <signal.h>

#include "netvid/check.h"
#include "netvid/linux_framebuffer.h"

#include "downsample/parallel_process.cpp"

#include "dpi.h"
#include "common.h"

namespace po=boost::program_options;

static volatile bool interrupted = false;

void interrupt_handler(int)
{
	interrupted = true;
}

int main(int argc, char **argv)
{
	signal(SIGINT, interrupt_handler);

	try
	{
		po::options_description desc("Allowed options");
		bool staggered_temporal_dithering;
		bool temporal_dithering;
		int fill_color=-1;

		desc.add_options()
			("help", "produce help message")
			("emulate", "Emulate CGA output through VGA")
			("temporal-dithering", po::bool_switch(&temporal_dithering)->default_value(false), "Uses flickering to produce more colors (arg: client, server)")
			("staggered-temporal-dithering", po::bool_switch(&staggered_temporal_dithering)->default_value(false), "Stagger temporal dithering")
			("fill-color", po::value(&fill_color), "Fill color")
			;

		po::variables_map vm;

		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count("help"))
		{
			std::cout << desc << std::endl;

			return 1;
		}

		po::notify(vm);

		const auto palette_fmt=fmt_a8r8g8b8;
		std::vector<std::uint32_t> palette;

		for (auto &c : cga_palette())
			palette.push_back(from_float_srgb(palette_fmt, to_srgb(c)));

		std::vector<std::uint8_t> palette_indices;

		if (temporal_dithering)
		{
			for (std::uint8_t i = 0; i < palette.size(); ++i)
			{
				for (std::uint8_t j = i; j < palette.size(); ++j)
				{
					palette_indices.push_back((j << 4)+i);
				}
			}
		} else
		{
			palette_indices.resize(palette.size());

			std::iota(palette_indices.begin(), palette_indices.end(), 0);
		}


		linux_framebuffer fb(framebuffer::fb_path, framebuffer::tty_path);
		auto &screen=fb.screen;
		frame_data_managed test_image;

		test_image.resize(framebuffer::width, framebuffer::height, framebuffer::bpp*(temporal_dithering ? 2 : 1));

		{
			test_image.clear();

			std::uint8_t white=15;

			if (temporal_dithering)
				white|=15 << 4;

			for (int y=0; y<test_image.height; ++y)
			{
				test_image.pixel_unaligned<std::uint8_t>(0, y)=white;
				test_image.pixel_unaligned<std::uint8_t>(test_image.width-1, y)=white;
			}

			for (int x=1; x<test_image.width-1; ++x)
			{
				test_image.pixel_unaligned<std::uint8_t>(x, 0)=white;
				test_image.pixel_unaligned<std::uint8_t>(x, test_image.height-1)=white;
			}

			int y_tiles=(int)sqrt(palette_indices.size());
			int x_tiles=(palette_indices.size()+y_tiles-1)/y_tiles;

			for (int y=2; y<test_image.height-2; ++y)
			{
				int y_tile=(y-2)*y_tiles/(test_image.height-4);

				for (int x=2; x<test_image.width-2; ++x)
				{
					int x_tile=(x-2)*x_tiles/(test_image.width-4);
					std::uint8_t col_uint=palette_indices[y_tile+x_tile*y_tiles];

					if ((x%2)==(y%2) && temporal_dithering && staggered_temporal_dithering)
						col_uint=((col_uint & 0xf) << 4)+(col_uint >> 4);

					test_image.pixel_unaligned<std::uint8_t>(x, y)=col_uint;
				}
			}

			if (fill_color>=0)
				std::fill(test_image.data, test_image.end(), fill_color+(fill_color << 4));
		}

		fb.hide_cursor();

		unsigned int new_frames=std::numeric_limits<unsigned int>::max()-1;
		const bool emulate_cga=vm.count("emulate")>0;
		typedef std::chrono::high_resolution_clock clock_t;
		auto start_time=clock_t::now();
		int frames=0;
		parallel_process pp;

		pp.render_passes.emplace_back(
				[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
				{
				},
				[&] (const frame_data &in, frame_data &out, const render_context &ctx)
				{
					blt_fit(in, fb.screen, { emulate_cga, palette, boost::none, ctx.thread_idx, ctx.num_threads, frames });
				});

		for (; !interrupted;)
		{
			fb.wait_for_vsync();

			static frame_data_managed dummy;

			pp(test_image, dummy);

			++new_frames;
			++frames;

			if (new_frames>60)
			{
				new_frames=0;
				fb.wake_up();
			}
		}

		auto end_time=clock_t::now();
		auto dur=end_time-start_time;

		std::cout << frames*1000.0/std::chrono::duration_cast<std::chrono::milliseconds>(dur).count() << " frames/second" << std::endl;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
#endif
