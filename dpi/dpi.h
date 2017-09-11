#ifndef DPI_H
#define DPI_H

#include "common/cga.h"

namespace framebuffer
{
	// 33222222222211111111110000000000
	// 10987654321098765432109876543210
	// AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB
	static const int red_bit=23;
	static const int green_bit=15;
	static const int blue_bit=7;
	static const int intensity_bit=14;

	static const char *fb_path="/dev/fb0";
	static const char *tty_path="/dev/tty0";
}

#endif /* DPI_H */
