/**
 * Tries to signal HSYNC and VSYNC using clock_nanosleep
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

	if (!lock_memory())
		return -3;

	stack_prefault();

	gpio_init(gpio_output,
			gpio_map::vsync,
			gpio_map::hsync);

	write_performance_monitor_control(cycles_flag::enable_all_counters|cycles_flag::reset_count_register_one_and_two);

	monitor_timing::start();

	auto wait_rows = monitor_timing::wait_rows_hsync;

	for (int frame = 0; frame<600; ++frame)
	{
		wait_rows(
				monitor_timing::vscan_top_blanking+
				monitor_timing::vscan_top_overscan+
				monitor_timing::vscan_visible+
				monitor_timing::vscan_bottom_overscan+
				monitor_timing::vscan_bottom_blanking);

		gpio_set(gpio_map::vsync) = true;
		
		wait_rows(monitor_timing::vscan_sync);

		gpio_clr(gpio_map::vsync) = true;
	}

	return 0;
}
