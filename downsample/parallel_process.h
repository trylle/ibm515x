#ifndef parallel_process_h__
#define parallel_process_h__

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>

#include "netvid/framebuffer.h"

struct render_context
{
	int thread_idx=0;
	int num_threads=0;

	inline std::pair<int, int> rows(int height) const
	{
		int begin_row=(height*thread_idx)/num_threads;
		int end_row=(height*(thread_idx+1))/num_threads;

		return std::make_pair(begin_row, end_row);
	};
};

struct parallel_process
{
	struct render_pass_t
	{
		typedef std::function<void(const frame_data &in, render_pass_t &pass)> init_t;
		typedef std::function<void(const frame_data &in, frame_data &out, const render_context &ctx)> render_t;

		init_t init;
		render_t render;
		frame_data_managed frame;
		bool no_output=false;
	
		render_pass_t(render_pass_t::init_t init=nullptr, render_pass_t::render_t render=nullptr)
			: init(init), render(render)
		{

		}
	};

	std::vector<render_pass_t> render_passes;
	boost::asio::io_service io_service;
	std::vector<std::thread> threads;
	std::mutex mutex;
	std::condition_variable cv;
	boost::optional<boost::asio::io_service::work> work;

	parallel_process();
	~parallel_process();

	void operator()(const frame_data &in, frame_data_managed &out);
};

#endif // parallel_process_h__
