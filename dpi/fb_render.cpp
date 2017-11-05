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
#include <cassert>

#include <boost/program_options.hpp>

#include "netvid/check.h"
#include "netvid/linux_framebuffer.h"
#include "netvid/protocol.h"
#include "netvid/net.h"

#include "downsample/parallel_process.cpp"

#include "common/cga.h"

#include "dpi.h"
#include "common.h"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
namespace po=boost::program_options;

int main(int argc, char **argv)
{
	try
	{
		po::options_description desc("Allowed options");
		boost::optional<int> flicker_select;
		std::array<int, 2> offset = { 0, 0 };
		std::array<int, 2> scale = { -1, -1 };

		desc.add_options()
			("help", "produce help message")
			("recv", po::value<std::string>()->required(), "recv [ip:port]")
			("emulate", "Emulate CGA output through VGA")
			("flicker-select", po::value<int>(), "Select flicker frame [0,1]")
			("offset", po::value<std::string>(), "Offset frame in pixels <x,y>")
			("scale", po::value<std::string>(), "Force pixel scaling <x,y>")
			;

		po::variables_map vm;

		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count("help"))
		{
			std::cout << desc << std::endl;

			return 1;
		}

		if (vm.count("flicker-select"))
			flicker_select=vm["flicker-select"].as<int>();

		const bool emulate_cga=vm.count("emulate")>0;

		std::regex re_csv(R"(^([\+-]?\d+),([\+-]?\d+)$)");

		if (vm.count("offset"))
		{
			std::smatch sm;
			std::regex_match(vm["offset"].as<std::string>(), sm, re_csv);

			for (int i=0; i<2; i++)
				offset[i]=std::stoi(sm.str(i+1));
		}

		if (vm.count("scale"))
		{
			std::smatch sm;
			std::regex_match(vm["scale"].as<std::string>(), sm, re_csv);

			for (int i=0; i<2; i++)
				scale[i]=std::stoi(sm.str(i+1));
		}

		po::notify(vm);

		linux_framebuffer fb(framebuffer::fb_path, framebuffer::tty_path);

		fb.hide_cursor();

		netvid::io_service_wrapper io_service;
		netvid::socket_wrapper socket(io_service.io_service);

		socket.bind(vm["recv"].as<std::string>());

		netvid::frame_receiver fr(socket);
		netvid::sender<netvid::unlimited_sender> s(socket);

		const auto palette_fmt=fmt_a8r8g8b8;
		std::vector<std::uint32_t> palette;

		for (auto &c : cga_palette())
			palette.push_back(from_float_srgb(palette_fmt, to_srgb(c)));

		auto original_on_packet=std::move(fr.on_packet);
		boost::asio::ip::udp::endpoint last_endpoint;

		fr.on_packet=[&] (const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
		{
			last_endpoint=remote_endpoint;
			original_on_packet(data_begin, data_end, remote_endpoint);
		};

		remote_vsync_header vsync;
		std::chrono::steady_clock::time_point last_frame;
		std::chrono::steady_clock::time_point last_poke;
		auto time_since_last_frame=std::chrono::steady_clock::duration::max();
		auto time_since_last_poke=std::chrono::steady_clock::duration::max();
		remote_mode_header last_rmh;

		auto original_mode_set=fr.on_mode_set;

		fr.on_mode_set=[&] (const remote_mode_header &header)
		{
			original_mode_set(header);

			if (last_rmh==header)
				return;

			last_rmh=header;
			std::cout << "Mode changed to " << header.width << "x" << header.height << " " << header.bpp << "bpp aspect=" << header.aspect_ratio << ", clearing screen..." << std::endl;
			fb.wake_up();
			std::fill(fb.screen.data, fb.screen.data+fb.screen.bytes(), 0);
		};

		fr.on_frame=[&]
		{
			last_frame=std::chrono::steady_clock::now();
		};

		parallel_process pp;
		int frame_idx=0;

		pp.render_passes.emplace_back(
				[=] (const frame_data &in, parallel_process::render_pass_t &render_pass)
				{
				},
				[&] (const frame_data &in, frame_data &out, const render_context &ctx)
				{
					if (in.width!=int(last_rmh.width) || in.height!=int(last_rmh.height))
						return; // out-of-sync, don't render this frame as it screws up screen clear

					if (scale[0]==-1)
						blt_fit(in, fb.screen, { emulate_cga, palette, flicker_select, ctx.thread_idx, ctx.num_threads, frame_idx, offset });
					else
						blt(in, fb.screen, scale[0], scale[1], { emulate_cga, palette, flicker_select, ctx.thread_idx, ctx.num_threads, frame_idx, offset });
				});

		fr.start();

		io_service.run();

		for (;;)
		{
			fr.process_packets();

			auto now=std::chrono::steady_clock::now();
			
			time_since_last_frame=now-last_frame;

			if (std::chrono::duration_cast<std::chrono::seconds>(time_since_last_frame).count()<1)
			{
				time_since_last_poke=now-last_poke;
			
				if (std::chrono::duration_cast<std::chrono::seconds>(time_since_last_poke).count()>=1)
				{
					fb.wake_up();

					last_poke=now;
				}

				s.set_remote_endpoint(last_endpoint);
				io_service.io_service.post([&] { s.send([] (auto, auto) {}, vsync); });

				fb.wait_for_vsync();

				const auto &buffer=fr.front_buffer;
				static frame_data_managed dummy;

				pp(buffer, dummy);
				++frame_idx;
			}
			else
			{
				std::cout << "Waiting for frame..." << std::endl;
				fr.wait_for_frame();
			}
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
#endif
