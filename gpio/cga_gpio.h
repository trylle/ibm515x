#ifndef CGA_GPIO_H
#define CGA_GPIO_H

#include "gpio.h"
#include "cycles.h"
#include "rt.h"

namespace gpio_map
{
	static const int vsync = 8;
	static const int hsync = 7;
	static const int red = 10;
	static const int green = 9;
	static const int blue = 11;
	static const int intensity = 4;
}

struct combined_clock
{
	static const int host_device_cycles_per_s = 700*1000*1000;

	busy_wait_cycles bwc;
	rt_clock rtc;

	void mark()
	{
		rtc.mark();
		bwc.mark();
	}

	void accumulate(int ns)
	{
		rtc.accumulate(ns);
		bwc.accumulate(ns_to_cycles(ns));
	}

	void wait()
	{
		rtc.wait();
		bwc.mark();
	}

	void busy_wait()
	{
		bwc.wait();
	}

	void wait(int ns)
	{
		rtc.wait(ns);
		bwc.mark();
	}

	void busy_wait(int ns)
	{
		rtc.accumulate(ns);
		bwc.wait(ns_to_cycles(ns));
	}

	static int ns_to_cycles(int ns)
	{
		return (ns*std::int64_t(host_device_cycles_per_s))/(1000*1000*1000);
	}
};

namespace monitor_timing
{
	static combined_clock clock;

	static inline void start()
	{
		clock.mark();
	}

	static inline void accumulate_pixels(int pixels)
	{
		clock.accumulate(pixel_interval_ns(pixels));
	}

	static inline void wait()
	{
		clock.wait();
	}

	static inline void busy_wait()
	{
		clock.busy_wait();
	}

	// primarily for performance testing
	inline void wait_rows_no_hsync(int rows)
	{
		const int hscan_total=
			hscan_left_blanking+
			hscan_left_overscan+
			hscan_visible+
			hscan_right_overscan+
			hscan_right_blanking+
			hscan_sync;

		accumulate_pixels(hscan_total*rows);
		wait();
	}

	// delays processing for a set number of rows, while still emitting hsync signal
	inline void wait_rows_hsync(int rows)
	{
		for (int row = 0; row<rows; ++row)
		{
			accumulate_pixels(
				monitor_timing::hscan_left_blanking+
				monitor_timing::hscan_left_overscan+
				monitor_timing::hscan_visible+
				monitor_timing::hscan_right_overscan+
				monitor_timing::hscan_right_blanking);
			wait();

			gpio_set(gpio_map::hsync) = true;

			accumulate_pixels(monitor_timing::hscan_sync);
			busy_wait();

			gpio_clr(gpio_map::hsync) = true;
		}
	}
}

#endif /* CGA_GPIO_H */
