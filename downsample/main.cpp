/**
 * CGA downscaler
 */

#include <boost/program_options.hpp>
#include <boost/asio/high_resolution_timer.hpp>

#include "netvid/check.h"
#include "netvid/linux_framebuffer.h"
#include "netvid/protocol.h"
#include "netvid/net.h"

#include "common/cga.h"

#include "cga_downsample.h"
#include "bayer.h"
#include "hsp.h"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
namespace po=boost::program_options;

template<class timer_type, class duration_type, class handler_type>
void timer_reissuer(timer_type &timer, const duration_type &d, const handler_type &handler)
{
	auto issuer=[&timer, d] (const auto &handler) -> void
	{
		timer.expires_from_now(d);
		timer.async_wait([=] (const boost::system::error_code &ec) { handler(handler, ec); });
	};

	auto subhandler_impl=[=] (const auto &self, const boost::system::error_code &ec) -> void
	{
		handler(ec);
		issuer(self);
	};

	issuer(subhandler_impl);
}

template<class socket_type, class endpoint_type, class buffer_type, class handler_type>
void recv_from_reissuer(socket_type &socket, endpoint_type &endpoint, const buffer_type &buf, const handler_type &handler)
{
	auto issuer=[&socket, &endpoint, &buf] (const auto &handler) -> void
	{
		socket.async_receive_from(buf, endpoint, [=] (const boost::system::error_code &ec, std::size_t bytes_transferred)
				{ handler(handler, ec, bytes_transferred); });
	};

	auto subhandler_impl=[=] (const auto &self, const boost::system::error_code &ec, std::size_t bytes_transferred) -> void
	{
		handler(ec, bytes_transferred);
		issuer(self);
	};

	issuer(subhandler_impl);
}

std::array<int, 2> parse_vector2i(const std::string &s)
{
	std::regex re(R"(^(\d+)(,(\d+))?$)", std::regex::ECMAScript);
	std::smatch sm;
	std::array<int, 2> ret;

	std::regex_match(s, sm, re);

	if (sm.length(3)==0)
		ret.fill(std::stoi(sm.str(1)));
	else
		ret={ std::stoi(sm.str(1)), std::stoi(sm.str(3)) };

	return ret;
}

int main(int argc, char **argv)
{
	try
	{
		po::options_description desc("Allowed options");
		double local_contrast_gain=0;
		double local_contrast_stddev=.5;
		double black_crush_high=0;
		double black_crush_low=0;
		bool staggered_temporal_dithering=false;
		bool vsync_signal=false;

		desc.add_options()
			("help", "produce help message")
			("recv", po::value<std::string>()->required(), "<ip:port>")
			("send", po::value<std::string>()->required(), "<ip:port>")
			("algorithm", po::value<std::string>()->default_value("nearest"), "Downsampling algorithm (arg: nearest, bayer, temporal-error-diffusion)")
			("bayer-level", po::value<std::string>()->default_value("8"), "<n> or <rows,cols>")
			("temporal-dithering", po::value<std::string>(), "Uses flickering to produce more colors (arg: client, server)")
			("staggered-temporal-dithering", po::bool_switch(&staggered_temporal_dithering)->default_value(false), "Stagger temporal dithering")
			("local-contrast-gain", po::value<double>(&local_contrast_gain), "Local contrast gain")
			("local-contrast-stddev", po::value<double>(&local_contrast_stddev), "Local contrast standard deviance")
			("black-crush-high", po::value<double>(&black_crush_high), "Level at which to start crushing black")
			("black-crush-low", po::value<double>(&black_crush_low), "Level to consider pure black")
			("vsync-signal", po::bool_switch(&vsync_signal), "Listen to client VSYNC signal")
			("scale", po::value<std::string>()->default_value("1"), "Nearest neighbor pixel scaling (arg: <x,y>). Does not modify AR. Useful for 320x200->640x200 scaling to double dithering resolution")
			;

		po::variables_map vm;

		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count("help"))
		{
			std::cout << desc << std::endl;

			return 1;
		}

		po::notify(vm);

		boost::asio::io_service local_service;
		netvid::io_service_wrapper recv_service;
		netvid::io_service_wrapper send_service;
		netvid::socket_wrapper recv_socket(recv_service.io_service);
		netvid::socket_wrapper send_socket(send_service.io_service);

		recv_socket.bind(vm["recv"].as<std::string>());

		netvid::frame_receiver fr(recv_socket);
		netvid::sender<netvid::rate_limited_sender> s(send_socket);

		s.set_remote_endpoint(vm["send"].as<std::string>());

		frame_data_managed downscaled;
		frame_data_managed transmit_buffer;

		std::vector<std::uint8_t> vsync_recv_buffer(64*1024);
		boost::asio::ip::udp::endpoint vsync_recv_endpoint;
		boost::asio::high_resolution_timer deadline(local_service);
		bool frame_sent=false;
		std::promise<void> frame_sent_promise;
		std::future<void> frame_sent_future;
		parallel_process pp;

		pp.render_passes.emplace_back(linearize());

		{
			auto scale=parse_vector2i(vm["scale"].as<std::string>());

			if (scale!=std::array<int, 2>{1,1})
				pp.render_passes.emplace_back(nearest_scale(scale[0], scale[1]));
		}

		bayer::map bayer_map;

		{
			auto bayer_size=parse_vector2i(vm["bayer-level"].as<std::string>());

			bayer_map=bayer::generate(bayer_size[0], bayer_size[1]);
		}

		auto linear_palette=cga_palette();
		dither_lut_t dither_lut(linear_palette, [linear_palette] (const std::array<float, 3> &target_color)
		{
			return eval_nearest_dithered_color(linear_palette, allowed_dither, target_color);
		});

		if (black_crush_high>0)
			pp.render_passes.emplace_back(black_crush(black_crush_low, black_crush_high));

		if (local_contrast_gain!=0)
			add_local_contrast(pp.render_passes, local_contrast_stddev, local_contrast_gain, 0, 0);

		bool temporal_dithering_client=true;
		bool temporal_dithering=vm.count("temporal-dithering")>0;

		if (temporal_dithering)
			temporal_dithering_client=(vm["temporal-dithering"].as<std::string>()=="client");

		auto init_algorithm=[&] (auto output_algorithm)
		{
			typedef decltype(output_algorithm) output_algorithm_t;

			auto downsample_algorithm_str=vm["algorithm"].as<std::string>();

			if (downsample_algorithm_str=="nearest")
				pp.render_passes.emplace_back(nearest<output_algorithm_t>::create(linear_palette, output_algorithm));
			else if (downsample_algorithm_str=="bayer")
				pp.render_passes.emplace_back(bayer_r<output_algorithm_t>::create(bayer_map, dither_lut, output_algorithm));
			else if (downsample_algorithm_str=="temporal-error-diffusion")
				pp.render_passes.emplace_back(temporal_error_diffusion<output_algorithm_t>::create(linear_palette, output_algorithm));
			else if (downsample_algorithm_str=="passthrough")
				pp.render_passes.emplace_back(unlinearize(fmt_a8r8g8b8));
			else
				throw std::invalid_argument("invalid algorithm");
		};

		if (!temporal_dithering)
			init_algorithm(normal_output());
		else
		{
			async_temporal_dither_output tdo;

			std::tie(linear_palette, tdo.indices)=combine_palette(linear_palette);
			tdo.staggered=staggered_temporal_dithering;

			auto combine_allowed_dither=[linear_palette] (int left, int right)
			{
				auto left_color=linear_palette[left];
				auto right_color=linear_palette[right];
				auto left_hsp=rgb_to_hsp(left_color);
				auto right_hsp=rgb_to_hsp(right_color);

				auto hue_dist=fmod(std::abs(left_hsp[0]-right_hsp[0]), 1);
				auto has_color=left_hsp[1]>.25f && right_hsp[1]>.25f;
				auto value_dist=std::abs(left_hsp[2]-right_hsp[2]);

				return (hue_dist<.25f || !has_color) && value_dist<.15f;
			};

			dither_lut=dither_lut_t(linear_palette, [linear_palette, combine_allowed_dither] (const std::array<float, 3> &target_color)
			{
				return eval_nearest_dithered_color(linear_palette, combine_allowed_dither, target_color);
			});

			init_algorithm(tdo);
		}

		std::mutex processed_mutex;
		frame_data_managed processed_frame;

		auto process_current_frame=[&]
		{
			std::unique_lock<std::mutex> l(processed_mutex);

			if (processed_frame.bpp==8 && !temporal_dithering_client)
			{
				static int frame_idx=-1;

				++frame_idx;

				downscaled.resize(
					processed_frame.width,
					processed_frame.height,
					4);
				downscaled.aspect_ratio=processed_frame.aspect_ratio;

				for (int y=0; y<downscaled.height; ++y)
				{
					for (int x=0; x<downscaled.width; ++x)
					{
						auto i=*processed_frame.pixel<std::uint8_t>(x, y);

						normal_output::pp(downscaled, x, y,
							(frame_idx%2==0) ? (i >> 4) : (i%16));
					}
				}

				return;
			}

			downscaled.copy(processed_frame);
		};

		auto send_current_frame=[&]
		{
			if (frame_sent_future.valid())
				frame_sent_future.get();

			process_current_frame();

			std::swap(downscaled, transmit_buffer);

			if (transmit_buffer)
			{
				frame_sent_promise={};
				frame_sent_future=frame_sent_promise.get_future();
				send_service.io_service.post([&] { s.send(transmit_buffer, frame_sent_promise); });
			}
		};

		auto check_deadline=[&]
		{
			static int waiting_cycles=0;
			bool was_frame_sent=frame_sent;

			frame_sent=false;

			if (was_frame_sent)
			{
				waiting_cycles=0;

				return;
			}

			std::cout << "Too long since last VSYNC, forcing new frame (" << ++waiting_cycles << ")\r" << std::flush;
			send_current_frame();
		};

		if (vsync_signal)
		{
			std::cout << "Using remote VSYNC signal" << std::endl;

			timer_reissuer(deadline, std::chrono::milliseconds(1000/3), [&] (auto) { check_deadline(); });

			auto vsync_recv_handler=[&]
			{
				frame_sent=true;
				local_service.post(send_current_frame);
			};

			recv_from_reissuer(send_socket.socket, vsync_recv_endpoint, boost::asio::buffer(vsync_recv_buffer), [&] (const boost::system::error_code &ec, std::size_t bytes_transferred) { vsync_recv_handler(); });
		}

		fr.start();

		std::thread input_frame_processing_thread([&]
			{
				frame_data_managed internal_buffer;
				frame_data_managed tmp_buffer;

				for (;;)
				{
					fr.wait_for_frame();
					fr.process_packets();

					auto lock=fr.lock_front_buffer();
					const auto &in_buffer=fr.front_buffer;
					auto current_hash=std::hash<frame_data>()(in_buffer);
					static auto last_hash=~current_hash;

					if (in_buffer && current_hash!=last_hash)
					{
						if (in_buffer.width==640 && in_buffer.height==400 && std::abs(in_buffer.aspect_ratio-4/3.f)<1e-3f)
						{
							// dosbox annoyingly likes to render 640x200 as 640x400
							tmp_buffer.resize(640, 200, in_buffer.pitch, in_buffer.bpp);
							tmp_buffer.aspect_ratio=in_buffer.aspect_ratio;

							for (int y=0; y<200; ++y)
								std::copy(in_buffer.data+y*2*in_buffer.pitch, in_buffer.data+(y*2+1)*in_buffer.pitch, tmp_buffer.data+y*tmp_buffer.pitch);

							pp(tmp_buffer, internal_buffer);
						}
						else
							pp(in_buffer, internal_buffer);

						last_hash=current_hash;
					}

					std::unique_lock<std::mutex> l(processed_mutex);

					processed_frame.copy(internal_buffer);

					if (!vsync_signal)
						local_service.post(send_current_frame);
				}
			});

		recv_service.run();
		send_service.run();

#if __linux__
		{
			sched_param sp={ 90 };
			int policy=SCHED_FIFO;

			if (pthread_setschedparam(recv_service.thread.native_handle(), policy, &sp))
				std::perror("Failed to set priority");

			if (pthread_setschedparam(send_service.thread.native_handle(), policy, &sp))
				std::perror("Failed to set priority");
		}
#endif

		boost::asio::io_service::work work(local_service);

		local_service.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
