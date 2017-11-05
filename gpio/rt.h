#ifndef RT_H
#define RT_H

// adapted from https://rt.wiki.kernel.org/index.php/Squarewave-example

#include <iostream>

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/io.h>

#include <boost/optional.hpp>

static const int NSEC_PER_SEC=1000*1000*1000;
static const int MAX_SAFE_STACK=8*1024; /* The maximum stack size which is
                                           guaranteed safe to access without
                                           faulting */
static const int RT_PRIORITY=49; /* we use 49 as the PRREMPT_RT use 50
		                            as the priority of kernel tasklets
        		                    and interrupt handler by default */

/* using clock_nanosleep of librt */
extern int clock_nanosleep(clockid_t __clock_id, int __flags,
		__const struct timespec *__req,
		struct timespec *__rem);

/* the struct timespec consists of nanoseconds
 * and seconds. if the nanoseconds are getting
 * bigger than 1000000000 (= 1 second) the
 * variable containing seconds has to be
 * incremented and the nanoseconds decremented
 * by 1000000000.
 */
static inline void tsnorm(struct timespec *ts)
{
	while (ts->tv_nsec>=NSEC_PER_SEC)
	{
		ts->tv_nsec-=NSEC_PER_SEC;
		ts->tv_sec++;
	}
}

struct rt_clock
{
	const clockid_t clk_id = CLOCK_MONOTONIC; // Originally 0, which clock is this?
	timespec t;

	inline rt_clock(clockid_t clk_id = CLOCK_MONOTONIC)
		: clk_id(clk_id)
	{
	}

	inline void mark()
	{
		/* get current time */
		clock_gettime(clk_id, &t);
	}

	inline void accumulate(int ns)
	{
		/* calculate next shot */
		t.tv_nsec+=ns;

		tsnorm(&t);
	}

	inline void wait(int ns)
	{
		accumulate(ns);
		wait();
	}

	inline void wait()
	{
		clock_nanosleep(clk_id, TIMER_ABSTIME, &t, NULL);
	}
};


inline bool init_rt(const boost::optional<int> &sched_priority=boost::none)
{
	struct sched_param param;

	if (sched_priority)
	{
		param.sched_priority=*sched_priority;

		std::cerr << "using realtime, priority: " << param.sched_priority << std::endl;

		/* enable realtime fifo scheduling */
		if (sched_setscheduler(0, SCHED_FIFO, &param)==-1)
		{
			std::perror("sched_setscheduler failed");

			return false;
		}
	}

	return true;
}

inline void stack_prefault()
{
	std::uint8_t dummy[MAX_SAFE_STACK];

	std::fill(std::begin(dummy), std::end(dummy), 0);
}

inline bool lock_memory()
{
	/* Lock memory */

	if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
	{
		std::perror("mlockall failed");

		return false;
	}

	return true;
}

#endif /* RT_H */
