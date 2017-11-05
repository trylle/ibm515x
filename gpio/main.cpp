/**
 * Framebuffer driver for a CGA monitor
 */

#include "pch.h"

#include <cstdint>
#include <array>

extern "C"
{
#include "PJ_RPI/PJ_RPI.h"
}

#include "common/cga.h"

#include "rt.h"
#include "gpio.h"
#include "cga_gpio.h"

int main()
{
	if (!init_rt(RT_PRIORITY))
	{
		std::cerr << "Failed to set real-time schedule priority" << std::endl;

		return -1;
	}

	if (map_peripheral(&gpio) == -1) 
	{
		std::cerr << "Failed to map the physical GPIO registers into the virtual memory space." << std::endl;

		return -2;
	}

	std::uint8_t pixels[framebuffer::bytes()] = {};

	// Initialize with test data
	{
		int count=0;

		for (auto &pix : pixels)
		{
			pix = count & 0x7;
			count++;
			pix |= (count & 0x7) << 4;
			count++;
		}
	}

	if (!lock_memory())
		return -3;

	stack_prefault();

	gpio_init(gpio_output,
			gpio_map::vsync,
			gpio_map::hsync,
			gpio_map::red,
			gpio_map::green,
			gpio_map::blue,
			gpio_map::intensity);

	write_performance_monitor_control(cycles_flag::enable_all_counters|cycles_flag::reset_count_register_one_and_two);

	monitor_timing::start();

	auto wait_rows = monitor_timing::wait_rows_hsync;

	for (int frame = 0; frame<2; ++frame)
	{
		wait_rows(
				monitor_timing::vscan_top_blanking+
				monitor_timing::vscan_top_overscan);

		for (int row = 0; row<framebuffer::height; ++row)
		{
			monitor_timing::accumulate_pixels(
					monitor_timing::hscan_left_blanking+
					monitor_timing::hscan_left_overscan);
			monitor_timing::wait();

			auto row_pixels = &pixels[row*framebuffer::pitch];

			for (int col = 0; col<framebuffer::width; ++col)
			{
				int pixel_offset = col%2==0 ? 0 : 4;
				auto current_pixel = *(row_pixels+col*framebuffer::bpp/8) >> pixel_offset;
				auto r = (current_pixel & (1 << 0))!=0;
				auto g = (current_pixel & (1 << 1))!=0;
				auto b = (current_pixel & (1 << 2))!=0;
				auto i = (current_pixel & (1 << 3))!=0;

				gpio_set(gpio_map::red, gpio_map::green, gpio_map::blue, gpio_map::intensity) = r, g, b, i;
				gpio_clr(gpio_map::red, gpio_map::green, gpio_map::blue, gpio_map::intensity) = !r, !g, !b, !i;

				monitor_timing::accumulate_pixels(monitor_timing::framebuffer_pixel_width);
				monitor_timing::busy_wait();
			}

			monitor_timing::accumulate_pixels(
				monitor_timing::hscan_right_blanking+
				monitor_timing::hscan_right_overscan);
			monitor_timing::wait();

			gpio_set(gpio_map::hsync) = true;
			
			monitor_timing::accumulate_pixels(monitor_timing::hscan_sync);
			monitor_timing::busy_wait();

			gpio_clr(gpio_map::hsync) = true;
		}

		wait_rows(
			monitor_timing::vscan_bottom_overscan+
			monitor_timing::vscan_bottom_blanking);

		gpio_set(gpio_map::vsync) = true;
	
		wait_rows(monitor_timing::vscan_sync);

		gpio_clr(gpio_map::vsync) = true;
	}

	return 0;
}
