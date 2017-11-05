#ifndef COMMON_H
#define COMMON_H

#include <utility>
#include <boost/optional.hpp>
#include <vector>

class frame_data;

extern std::pair<int, int> best_fit(float src_aspect_ratio, int dest_width, int dest_height, float dest_aspect_ratio);
extern std::pair<int, int> get_scaling_factors(int src_width, int src_height, float src_aspect_ratio, int dest_width, int dest_height, float dest_aspect_ratio);

struct blt_options
{
	bool emulate_cga;
	std::vector<std::uint32_t> palette;
	boost::optional<int> flicker_select;
	int y_div=0;
	int y_divs=1;
	int frame_count=0;
	std::array<int, 2> offset={ 0, 0 };
};

extern void blt(const frame_data &input, frame_data &output, int x_scaling, int y_scaling, const blt_options &options);
extern void blt_fit(const frame_data &input, frame_data &output, const blt_options &options);

#endif /* COMMON_H */
