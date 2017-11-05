#define BOOST_TEST_MODULE cga_downsample
#include <boost/test/included/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>

#include "netvid/framebuffer.h"

#include "cga_downsample.h"
#include "bayer.h"

namespace bdata=boost::unit_test::data;

#define BOOST_TEST_INFO_VAR(var) \
	BOOST_TEST_INFO("With parameter " #var " = " << (var))

#define BOOST_TEST_INFO_VAR_HEX(var) \
	BOOST_TEST_INFO("With parameter " #var " = " << std::hex << (var))

namespace std
{
	template<class T, std::size_t n>
	std::ostream &operator<<(std::ostream &stream, const typename std::array<T, n> &a)
	{
		bool first=true;

		stream << "std::array<" << typeid(T).name() << ", " << n << ">{ ";

		for (const auto &v : a)
		{
			if (!first)
				stream << ", ";

			stream << v;

			first=false;
		}

		stream << " }";

		return stream;
	}
}

BOOST_AUTO_TEST_CASE(test_main)
{
	std::array<float, 3> srgb{ 173/255.0f, 170/255.0f, 173/255.0f };
	auto linear=to_linear(srgb);
	auto cga=eval_nearest_color(cga_palette(), linear);

	BOOST_TEST(cga==7);
}

BOOST_AUTO_TEST_CASE(palette_check)
{
	std::vector<std::array<float, 3>> palette;
	
	auto add_color=[&] (std::uint32_t color)
	{
		palette.push_back(to_float_srgb(fmt_a8r8g8b8, color));
	};
	auto add_colors=[&] (std::vector<std::uint32_t> colors)
	{
		for (auto color : colors)
			add_color(color);
	};

	add_colors(
			{
				0x000000,
				0x0000aa,
				0x00aa00,
				0x00aaaa,
				0xaa0000,
				0xaa00aa,
				0xaa5500,
				0xaaaaaa,
				0x555555,
				0x5555ff,
				0x55ff55,
				0x55ffff,
				0xff5555,
				0xff55ff,
				0xffff55,
				0xffffff,
			});

	for (std::size_t i=0; i<palette.size(); ++i)
	{
		auto srgb=palette[i];
		auto linear=to_linear(srgb);
		auto cga=eval_nearest_color(cga_palette(), linear);

		BOOST_TEST(cga==i);
	}

	for (std::size_t i=0; i<palette.size(); ++i)
	{
		auto srgb=palette[i];
		auto linear=to_linear(srgb);
		auto cga=eval_nearest_dithered_color(cga_palette(), allowed_dither, linear);
		bool left_ok=(cga.left_color==i && std::abs(cga.mix-0)<1e-3f);
		bool right_ok=(cga.right_color==i && std::abs(cga.mix-1)<1e-3f);
		bool ok=(left_ok || right_ok);

		BOOST_TEST(ok, int(cga.left_color) << "," << int(cga.right_color) << "," << cga.mix << " did not equal palette idx " << i);
	}

	dither_lut_t dither_lut(cga_palette(), [] (const std::array<float, 3> &target_color)
	{
		return eval_nearest_dithered_color(cga_palette(), allowed_dither, target_color);
	});

	for (std::size_t i=0; i<palette.size(); ++i)
	{
		auto srgb=palette[i];
		auto linear=to_linear(srgb);
		auto cga=dither_lut.get(linear);
		bool left_ok=(cga.left_color==i && std::fabs(0-cga.mix)<1e-6f);
		bool right_ok=(cga.right_color==i && std::fabs(1-cga.mix)<1e-6f);
		bool ok=(left_ok || right_ok);

		BOOST_TEST(ok, int(cga.left_color) << "," << int(cga.right_color) << "," << cga.mix << " did not equal palette idx " << i);
	}

	for (std::size_t i=0; i<palette.size(); ++i)
	{
		const auto tol=1-0.925f;
		auto srgb=palette[i];

		BOOST_TEST_INFO_VAR(i);
		BOOST_TEST_INFO_VAR(palette[i]);

		auto srgb_bits=from_float_srgb(fmt_r5g6b5, srgb);

		srgb=to_float_srgb(fmt_r5g6b5, srgb_bits);

		BOOST_TEST_INFO_VAR_HEX(srgb_bits);
		BOOST_TEST_INFO_VAR(srgb);

		auto linear=to_linear(srgb);
		auto cga=dither_lut.get(linear);
		bool left_ok=(cga.left_color==i && std::fabs(0-cga.mix)<tol);
		bool right_ok=(cga.right_color==i && std::fabs(1-cga.mix)<tol);
		bool ok=(left_ok || right_ok);

		BOOST_TEST_INFO_VAR(cga.left_color);
		BOOST_TEST_INFO_VAR(cga.right_color);
		BOOST_TEST_INFO_VAR(cga.mix);

		BOOST_TEST(ok);
	}
}

std::vector<std::tuple<int, int, int, int>> bayer_largest_pre_dataset()
{
	return
	{
		{
			4, 4, 2, 2
		},
		{
			6, 6, 3, 3
		},
		{
			4, 1, 2, 1
		}
	};
}

BOOST_DATA_TEST_CASE(bayer_largest_pre, bdata::make(bayer_largest_pre_dataset()), in_rows, in_cols, out_rows, out_cols)
{
	auto m=bayer::get_largest_predefined_map(in_rows, in_cols);

	BOOST_TEST(m.rows()==out_rows);
	BOOST_TEST(m.cols()==out_cols);
}

BOOST_AUTO_TEST_CASE(bayer_2x2)
{
	std::array<int, 2*2> values=
	{
		0, 2,
		3, 1
	};
	auto gen=bayer::generate(2, 2);

	BOOST_TEST(values==gen.values, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bayer_2x4)
{
	std::array<int, 2*4> values=
	{
		0, 4, 1, 5,
		6, 2, 7, 3
	};
	auto gen=bayer::generate(2, 4);

	BOOST_TEST(values==gen.values, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bayer_4x2)
{
	std::array<int, 4*2> values=
	{
		0, 4,
		6, 2,
		1, 5,
		7, 3
	};
	auto gen=bayer::generate(4, 2);

	BOOST_TEST(values==gen.values, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bayer_4x4)
{
	std::array<int, 4*4> values=
	{
		 0,  8,  2, 10,
		12,  4, 14,  6,
		 3, 11,  1,  9,
		15,  7, 13,  5,
	};
	auto gen=bayer::generate(4, 4);

	BOOST_TEST(values==gen.values, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(bayer_8x8)
{
	static const int n=8;
	std::array<int, n*n> values=
	{
		0,32,8,40,2,34,10,42,
		48,16,56,24,50,18,58,26,
		12,44,4,36,14,46,6,38,
		60,28,52,20,62,30,54,22,
		3,35,11,43,1,33,9,41,
		51,19,59,27,49,17,57,25,
		15,47,7,39,13,45,5,37,
		63,31,55,23,61,29,53,21,
	};
	auto gen=bayer::generate(8, 8);

	BOOST_TEST(values==gen.values, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(temporal_dither)
{
	typedef std::array<float, 3> s;
	const auto tol=1e-3f;
	auto linear_palette=cga_palette();
	//std::vector<int> indices;

	std::tie(linear_palette, /*indices*/std::ignore)=combine_palette(linear_palette);

	auto allowed_dither=[linear_palette] (int left, int right)
	{
		auto max_distance=.25f;
		auto left_color=linear_palette[left];
		auto right_color=linear_palette[right];

		return distance(left_color, right_color)<max_distance;
	};

	{
		auto target_color=s({ 1, 1, 1 });
		auto cga=eval_nearest_dithered_color(linear_palette, allowed_dither, target_color);
		//auto cga=dither_lut.get(c);
		bool left_ok=(linear_palette[cga.left_color]==target_color && std::fabs(0-cga.mix)<tol);
		bool right_ok=(linear_palette[cga.right_color]==target_color && std::fabs(1-cga.mix)<tol);
		bool ok=(left_ok || right_ok);

		BOOST_TEST_INFO_VAR(linear_palette[cga.left_color]);
		BOOST_TEST_INFO_VAR(linear_palette[cga.right_color]);
		BOOST_TEST_INFO_VAR(cga.mix);

		BOOST_TEST(ok);
	}

	return;

	auto dither_lut=dither_lut_t(linear_palette, [linear_palette, allowed_dither] (const std::array<float, 3> &target_color)
	{
		return eval_nearest_dithered_color(linear_palette, allowed_dither, target_color);
	});

	//auto b=dither_lut.get(s({ 0, 0, 0 }));
}