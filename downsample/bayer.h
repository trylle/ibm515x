#ifndef bayer_h__
#define bayer_h__

#include <vector>

namespace bayer
{
	struct map
	{
		/*const */std::vector<int> values;
		/*const */int rows_;

		map(int rows=0, int cols=0);
		map(const std::vector<int> &values, int rows);

		// 		map(map &&)=default;
		// 		map(const map &)=default;
		// 
		// 		map &operator=(map &&)=default;
		// 		map &operator=(const map &)=default;

		int size() const;

		int rows() const;

		int cols() const;

		bool is_on(int x, int y, float mix_level) const;

		operator bool() const;

		bool operator!() const;
	};

	extern map get_predefined(int rows, int cols);
	extern map get_largest_predefined_map(int rows, int cols);
	extern map generate(int rows, int cols);
}

#endif // bayer_h__
