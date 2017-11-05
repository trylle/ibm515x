#ifndef CYCLES_H
#define CYCLES_H

/**
 * This code is adapted from https://www.raspberrypi.org/forums/viewtopic.php?f=63&t=155830
 * and http://blog.regehr.org/archives/794. Requires the kernel module from the latter site.
 */

#include "jss_bitmask/bitmask_operators.hpp"

enum class cycles_flag : unsigned int
{
	enable_all_counters=1 << 0,
	reset_count_register_one_and_two=1 << 1,
	reset_cycle_counter_register=1 << 2,
	cycle_count_divider=1 << 3 // divides by 64
};

template<>
struct enable_bitmask_operators<cycles_flag>
{
    static const bool enable=true;
};

inline void write_performance_monitor_control(cycles_flag flags)
{
#ifdef __arm__
	__asm__ volatile("mcr p15, 0, %0, c15, c12, 0" :: "r"(flags));
#else
#pragma message "Unsupported architecture"
#endif
}

inline unsigned int read_performance_monitor_control()
{
#ifdef __arm__
	unsigned int v;

	__asm__ volatile("mrc p15, 0, %0, c15, c12, 1" : "=r"(v));

	return v;
#else
	return 0;
#endif
}

struct busy_wait_cycles
{
	unsigned int start_cycle = 0;
	unsigned int cycles_accumulated = 0;

	inline void mark()
	{
		start_cycle = read_performance_monitor_control();
		cycles_accumulated = 0;
	}

	inline void accumulate(unsigned int cycles)
	{
		cycles_accumulated += cycles;
	}

	inline void wait(unsigned int cycles)
	{
		accumulate(cycles);
		wait();
	}

	inline void wait()
	{
		while (read_performance_monitor_control()-start_cycle<cycles_accumulated);

		start_cycle += cycles_accumulated;
		cycles_accumulated = 0;
	}
};

#endif /* CYCLES_H */
