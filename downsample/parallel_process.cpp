#include "parallel_process.h"

parallel_process::parallel_process()
{
	work.emplace(io_service);

	threads.reserve(std::thread::hardware_concurrency());

	for (unsigned int i=0; i<std::thread::hardware_concurrency(); ++i)
		threads.emplace_back([this] { io_service.run(); });
}


parallel_process::~parallel_process()
{
	work.reset();

	for (auto &t : threads)
		t.join();
}

void parallel_process::operator()(const frame_data &in, frame_data_managed &out)
{
	const frame_data *current_in=&in;

	for (auto i=render_passes.begin(); i!=render_passes.end(); ++i)
	{
		auto &render_pass=*i;

		if (i==--render_passes.end())
			std::swap(render_pass.frame, out);

		render_pass.init(*current_in, render_pass);

		int working_threads=threads.size();

		render_context ctx;

		ctx.num_threads=working_threads;

		for (int i=0; i<int(threads.size()); ++i)
		{
			ctx.thread_idx=i;

			io_service.post([this, &working_threads, &render_pass, current_in, ctx]
			{
				render_pass.render(*current_in, render_pass.frame, ctx);

				std::lock_guard<std::mutex> lk(mutex);

				--working_threads;

				cv.notify_one();
			});
		}

		{
			std::unique_lock<std::mutex> lk(mutex);

			cv.wait(lk, [&] { return working_threads==0; });
		}

		if (i==--render_passes.end())
			std::swap(render_pass.frame, out);

		if (!render_pass.no_output)
			current_in=&render_pass.frame;
	}
}
