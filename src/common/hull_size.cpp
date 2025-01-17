#include "hull_size.h"

hull_sizes const standard_hull_sizes{
	// 0x0x0
	std::array<double3_array, 2>{ double3_array{ 0, 0, 0 },
								  double3_array{ 0, 0, 0 } },
	// 32x32x72
	std::array<double3_array, 2>{ double3_array{ -16, -16, -36 },
								  double3_array{ 16, 16, 36 } },
	// 64x64x64
	std::array<double3_array, 2>{ double3_array{ -32, -32, -32 },
								  double3_array{ 32, 32, 32 } },
	// 32x32x36
	std::array<double3_array, 2>{ double3_array{ -16, -16, -18 },
								  double3_array{ 16, 16, 18 } }
};
