#include "common.h"

#include "netvid/framebuffer.h"

#include "dpi.h"

std::pair<int, int> best_fit(float src_aspect_ratio, int dest_width, int dest_height, float dest_aspect_ratio)
{
	float candidate_width=src_aspect_ratio;
	float candidate_height=1;
	float s=dest_aspect_ratio/src_aspect_ratio;

	if (s<1)
	{
		candidate_width*=s;
		candidate_height*=s;
	}

	candidate_width*=dest_height;
	candidate_height*=dest_height;

	auto dest_pixel_aspect=dest_width/(dest_height*dest_aspect_ratio);

	candidate_width*=dest_pixel_aspect;

	return { floor(candidate_width+.5f), floor(candidate_height+.5f) };
}

std::pair<int, int> get_scaling_factors(int src_width, int src_height, float src_aspect_ratio, int dest_width, int dest_height, float dest_aspect_ratio)
{
	int width;
	int height;

	std::tie(width, height)=best_fit(src_aspect_ratio, dest_width, dest_height, dest_aspect_ratio);

	int x_s=width/src_width;
	int y_s=height/src_height;

	if (y_s==0)
		y_s=-src_height/height; // dosbox likes to use 640x400 when rendering 640x200 for some reason, so we need to support line skip

	return { std::max(1, x_s), std::max(1, y_s) };
}

void blt_(const frame_data &buffer, frame_data &screen, int x_scaling, int y_scaling, const blt_options &options)
{
	auto frame_count=std::uint32_t(options.frame_count);

	if (options.flicker_select)
		frame_count=std::uint32_t(*options.flicker_select);

	if (!options.emulate_cga && (x_scaling<3 || (buffer.bpp!=4 && buffer.bpp!=8)))
		return; // cga monitor safeties

	int cx=screen.width/2-buffer.width*x_scaling/2;
	int cy=screen.height/2-buffer.height*y_scaling/2;

	cx+=options.offset[0];
	cy+=options.offset[1];

	int bx=cx;
	int ex=cx+buffer.width*x_scaling;
	int by=cy;
	int ey=cy+buffer.height*y_scaling;

	bx=std::max(0, std::min(bx, screen.width-1));
	by=std::max(0, std::min(by, screen.height-1));
	ex=std::max(0, std::min(ex, screen.width));
	ey=std::max(0, std::min(ey, screen.height));

	bx=std::max(cx, std::min(bx, cx+buffer.width*x_scaling-1));
	by=std::max(cy, std::min(by, cy+buffer.height*y_scaling-1));
	ex=std::max(cx, std::min(ex, cx+buffer.width*x_scaling));
	ey=std::max(cy, std::min(ey, cy+buffer.height*y_scaling));

	bx-=cx;
	by-=cy;
	ex-=cx;
	ey-=cy;

	bx=(bx+x_scaling-1)/x_scaling;
	by=(by+y_scaling-1)/y_scaling;
	ex/=x_scaling;
	ey/=y_scaling;

	assert(bx>=0 && bx<buffer.width);
	assert(by>=0 && by<buffer.height);
	assert(ex>=0 && ex<=buffer.width);
	assert(ey>=0 && ey<=buffer.height);

	{
		int h=ey-by;

		ey=by+h*(options.y_div+1)/options.y_divs;
		by=by+h*options.y_div/options.y_divs;
	}

	for (int y=by; y<ey; ++y)
	{
		for (int x=bx; x<ex; ++x)
		{
			std::uint32_t col_uint=0;

			switch (buffer.bpp)
			{
			case 4:
			case 8:
				{
					int shr=(x%2==1) ? 4 : 0;
					int idx;

					if (buffer.bpp==4)
						idx=(*buffer.pixel<std::uint8_t>(x, y) >> shr) & 0xf;
					else
						idx=(*buffer.pixel<std::uint8_t>(x, y) >> (4*(frame_count%2))) & 0xf;

					if (options.emulate_cga)
					{
						col_uint=options.palette[idx];
					}
					else
					{
						col_uint|=(idx & 0x1) << (framebuffer::blue_bit-0);
						col_uint|=(idx & 0x2) << (framebuffer::green_bit-1);
						col_uint|=(idx & 0x4) << (framebuffer::red_bit-2);
						col_uint|=(idx & 0x8) << (framebuffer::intensity_bit-3);
					}
				}
				break;
			case 16:
				col_uint=from_float_srgb(fmt_a8r8g8b8, to_float_srgb(fmt_r5g6b5, *buffer.pixel<std::uint16_t>(x, y)));
				break;
			case 32:
				col_uint=*buffer.pixel<std::uint32_t>(x, y);
				break;
			}

			for (int y_s=0; y_s<y_scaling; ++y_s)
			{
				for (int x_s=0; x_s<x_scaling; ++x_s)
					*screen.pixel<std::uint32_t>(x*x_scaling+x_s+cx, y*y_scaling+y_s+cy)=col_uint;
			}
		}
	}
}

void blt(const frame_data &buffer, frame_data &screen, int x_scaling, int y_scaling, const blt_options &options)
{
	auto optimize=[&] (int x_scaling_opt, int y_scaling_opt, bool emulate_cga_opt, const boost::optional<int> &flicker_select_opt)
	{
		if (x_scaling!=x_scaling_opt || y_scaling!=y_scaling_opt || options.emulate_cga!=emulate_cga_opt || flicker_select_opt!=options.flicker_select)
			return false;

		blt_(buffer, screen, x_scaling_opt, y_scaling_opt, { emulate_cga_opt, options.palette, flicker_select_opt, options.y_div, options.y_divs, options.frame_count, options.offset });
		
		return true;
	};

	optimize(6, 1, false, boost::none) ||
	optimize(3, 1, false, boost::none) ||
	optimize(6, 1, false, 0) ||
	optimize(3, 1, false, 0) ||
	optimize(6, 1, false, 1) ||
	optimize(3, 1, false, 1) ||
	optimize(1, 1, false, boost::none) ||
	optimize(1, 1, false, 0) ||
	optimize(1, 1, false, 1) ||
	optimize(1, 1, true, boost::none) ||
	optimize(1, 1, true, 0) ||
	optimize(1, 1, true, 1) ||
	(blt_(buffer, screen, x_scaling, y_scaling, options), true);
}

void blt_fit(const frame_data &buffer, frame_data &screen, const blt_options &options)
{
	int x_scaling;
	int y_scaling;
	float screen_ar=screen.width/float(screen.height);

	if (!options.emulate_cga)
		screen_ar=4/3.f;

	std::tie(x_scaling, y_scaling)=get_scaling_factors(buffer.width, buffer.height, buffer.aspect_ratio, screen.width, screen.height, screen_ar);

	if (!options.emulate_cga)
		x_scaling=std::max(x_scaling, 3); // safety due to rpi pixel clock hack

	blt(buffer, screen, x_scaling, y_scaling, options);
}
