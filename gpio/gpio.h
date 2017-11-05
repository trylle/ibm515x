#ifndef GPIO_H
#define GPIO_H

#include <cstdint>
#include <array>

extern "C"
{
#include "PJ_RPI/PJ_RPI.h"
}

enum gpio_access_type
{
	gpio_input=0,
	gpio_output
};

template<typename... T>
void gpio_init(gpio_access_type gpio_access, T ... pins)
{
	for (int pin : { pins... })
	{
		INP_GPIO(pin);

		if (gpio_access==gpio_output)
			OUT_GPIO(pin);
	}
}

template<bool set, int pins>
class outputter
{
	int idx = -1;
	const std::array<int, pins> pin_array={};
	int bits = 0;

public:
	outputter(const std::array<int, pins> pin_array)
	: pin_array(pin_array)
	{

	}

	~outputter()
	{
		if (bits==0)
			return;

		if (set)
			GPIO_SET = bits;
		else
			GPIO_CLR = bits;
	}

	void push(bool value)
	{
		++idx;

		if (value)
		    bits|=1 << pin_array[idx];
	}

	outputter<set, pins> &operator=(bool value)
	{
		push(value);

		return *this;
	}

	outputter<set, pins> &operator,(bool value)
	{
		push(value);

		return *this;
	}
};

template<typename... T>
auto gpio_set(T ... pins)
{
	outputter<true, sizeof...(T)> o({ pins ... });

	return o;
}

template<typename... T>
auto gpio_clr(T ... pins)
{
	outputter<false, sizeof...(T)> o({ pins... });

	return o;
}

#endif /* GPIO_H */
