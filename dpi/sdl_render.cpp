/**
 * Linux framebuffer CGA renderer
 */

#include <cassert>

#include <boost/program_options.hpp>

#include <SDL2/SDL.h>

#include "netvid/check.h"
#include "netvid/linux_framebuffer.h"
#include "netvid/protocol.h"
#include "netvid/net.h"

#include "common/cga.h"

#include "dpi.h"
#include "common.h"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
namespace po=boost::program_options;

inline bool sdl_check(bool result, const char *code, const char *file, int line)
{
	if (result)
	{
		std::cerr << code << " failed! SDL_Error: " << SDL_GetError() << std::endl;

		exit(-1);
	}

	return result;
}

#define SDL_CHECK(x) \
	sdl_check((x), #x, __FILE__, __LINE__)

struct sdl_init
{
	sdl_init(Uint32 flags)
	{
		SDL_CHECK(SDL_Init(flags)<0);

	}
	~sdl_init()
	{
		SDL_Quit();
	}
};

int main(int argc, char **argv)
{
	try
	{
		po::options_description desc("Allowed options");
		boost::optional<int> flicker_select;

		desc.add_options()
			("help", "produce help message")
			("recv", po::value<std::string>()->required(), "recv [ip:port]")
			("emulate", "Emulate CGA output through VGA")
			//("flicker-select", po::value(&flicker_select), "Select flicker frame [0,1]")
			("flicker-select", po::value<int>(), "Select flicker frame [0,1]")
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

		po::notify(vm);

		sdl_init sdl(SDL_INIT_VIDEO);

		std::shared_ptr<SDL_Window> window;

		static const int SCREEN_WIDTH=640;
		static const int SCREEN_HEIGHT=200;
		
		window.reset(SDL_CreateWindow("sdl_render", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
 
		if (SDL_CHECK(!window))
			return -2;

		std::shared_ptr<SDL_Renderer> renderer;

		renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC), SDL_DestroyRenderer);

		if (SDL_CHECK(!renderer))
			return -3;

		std::shared_ptr<SDL_Texture> texture;

		texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT), SDL_DestroyTexture);

		if (SDL_CHECK(!texture))
			return -4;

		netvid::io_service_wrapper io_service;
		netvid::socket_wrapper socket(io_service.io_service);

		socket.bind(vm["recv"].as<std::string>());

		netvid::frame_receiver fr(socket);
		netvid::sender<netvid::unlimited_sender> s(socket);

		const bool emulate_cga=vm.count("emulate")>0;
		const auto palette_fmt=fmt_a8r8g8b8;
		std::vector<std::uint32_t> palette;

		for (const auto &c : cga_palette())
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
		remote_mode_header last_rmh;

		auto original_mode_set=fr.on_mode_set;

		auto lock_screen=[&] ()
		{
			static frame_data fb;
			std::shared_ptr<frame_data> sfb;
			Uint32 format;
			int access;

			SDL_CHECK(SDL_QueryTexture(texture.get(), &format, &access, &fb.width, &fb.height)<0);
			SDL_CHECK(SDL_LockTexture(texture.get(), nullptr, (void **)&fb.data, &fb.pitch)<0);
			fb.bpp=32;

			sfb.reset(&fb, [&] (auto)
				{
					SDL_UnlockTexture(texture.get());
					//SDL_RenderPresent(renderer.get());
				});

			return sfb;
		};

		fr.on_mode_set=[&] (const remote_mode_header &header)
		{
			original_mode_set(header);

			if (last_rmh==header)
				return;

			last_rmh=header;
			std::cout << "Mode changed to " << header.width << "x" << header.height << " " << header.bpp << "bpp aspect=" << header.aspect_ratio << ", clearing screen..." << std::endl;
		
			texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, header.width, header.height), SDL_DestroyTexture);

			if (SDL_CHECK(!texture))
				exit(-4);

			auto sfb = lock_screen();

			std::fill(sfb->data, sfb->data+sfb->bytes(), 0);
		};

		fr.on_frame=[&]
		{
			last_frame=std::chrono::steady_clock::now();
		};

		auto blt_frame=[&]
		{
			auto sfb=lock_screen();
			auto &screen=*sfb;
			auto &buffer=fr.front_buffer;
			static int frame_idx=0;

			blt(buffer, screen, 1, 1, { emulate_cga, palette, flicker_select, 0, 1, frame_idx });
			++frame_idx;
		};

		fr.start();

		io_service.run();

		for (;;)
		{
			SDL_Event event;
			
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
					case SDL_QUIT:
						{
							//io_service.stop();

							//return 0;
							exit(0);
						}
						break;
				}
			}

			fr.process_packets();
			
			auto now=std::chrono::steady_clock::now();
			
			time_since_last_frame=now-last_frame;

			if (std::chrono::duration_cast<std::chrono::seconds>(time_since_last_frame).count()<1)
			{
				s.set_remote_endpoint(last_endpoint);
				io_service.io_service.post([&] { s.send([] (auto, auto) {}, vsync); });

				SDL_Rect dest;
				int width=0;
				int height=0;

				dest.x=0;
				dest.y=0;

				SDL_GetWindowSize(window.get(), &width, &height);

				std::tie(dest.w, dest.h)=best_fit(fr.front_buffer.aspect_ratio, width, height, width/float(height)); // Assume square pixels on SDL

				dest.x=width/2-dest.w/2;
				dest.y=height/2-dest.h/2;

				blt_frame();

				SDL_RenderClear(renderer.get());
				SDL_RenderCopy(renderer.get(), texture.get(), nullptr, &dest);
				SDL_RenderPresent(renderer.get());
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
