#include "bayer.h"

#include <stdexcept>

using namespace bayer;

map::map(int rows/*=0*/, int cols/*=0*/)
	: values(rows*cols), rows_(rows)
{

}

map::map(const std::vector<int> &values, int rows)
	: values(values), rows_(rows)
{

}

int map::size() const
{
	return values.size();
}

int map::rows() const
{
	return rows_;
}

int map::cols() const
{
	if (values.size()==0 || rows()==0)
		return 0;

	return values.size()/rows();
}

bool map::is_on(int x, int y, float mix_level) const
{
	auto threshold=static_cast<int>((1-mix_level)*values.size()+.5f);
	auto idx=x%cols()+(y%rows())*cols();

	return values[idx]>=threshold;
}

map::operator bool() const
{
	return values.size()>0;
}

bool map::operator!() const
{
	return !operator bool();
}

std::vector<int> get_predefined_(int rows, int cols)
{
	if ((rows==2 && cols==1) ||
		(rows==1 && cols==2))
	{
		return
		{
			0, 1,
		};
	}

	if ((rows==3 && cols==1) ||
		(rows==1 && cols==3))
	{
		return
		{
			0, 2, 1,
		};
	}

	if ((rows==3 && cols==2) ||
		(rows==2 && cols==3))
	{
		return
		{
			0, 4, 2,
			3, 1, 5,
		};
	}

	if (rows==2 && cols==rows)
	{
		return
		{
			0, 2,
			3, 1,
		};
	}

	if (rows==3 && cols==rows)
	{
		return
		{
			0, 7, 3,
			6, 5, 2,
			4, 1, 8,
		};
	}

	return{};
}

map bayer::get_predefined(int rows, int cols)
{
	return{ get_predefined_(rows, cols), rows };
}

map bayer::get_largest_predefined_map(int rows, int cols)
{
	map current;

	for (int y=1; y<=rows; ++y)
	{
		for (int x=1; x<=cols; ++x)
		{
			if (rows%y!=0 || cols%x!=0)
				continue;

			auto candidate=get_predefined(y, x);

			if (candidate.size()<=current.size())
				continue;

			current=candidate;
		}
	}

	return current;
}

map bayer::generate(int rows, int cols)
{
	auto outer=get_largest_predefined_map(rows, cols);

	if (outer.cols()==cols && outer.rows()==rows)
		return outer;

	if (outer.rows()<=0 || outer.cols()<=0)
		throw std::invalid_argument("Unsupported row/col count");

	auto inner=generate(rows/outer.rows(), cols/outer.cols());
	map ret(rows, cols);

	std::swap(inner, outer);

	for (int y=0; y<rows; ++y)
	{
		for (int x=0; x<cols; ++x)
		{
			auto &r=ret.values[y*cols+x];

			r=inner.values[(y%inner.rows())*inner.cols()+(x%inner.cols())]*outer.size();
			r+=outer.values[((y/inner.rows())%outer.rows())*outer.cols()+((x/inner.cols())%outer.cols())];
		}
	}

	return ret;
}
