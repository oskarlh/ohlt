#include "hull_size.h"

hull_sizes const standard_hull_sizes{
	std::array<double3_array, 2>{		  // 0x0x0
		  double3_array{ 0, 0, 0 },
								  double3_array{ 0, 0, 0 }   },
	std::array<double3_array, 2>{ // 32x32x72
 double3_array{ -16, -16, -36 },
								  double3_array{ 16, 16, 36 } },
	std::array<double3_array, 2>{ // 64x64x64
 double3_array{ -32, -32, -32 },
								  double3_array{ 32, 32, 32 } },
	std::array<double3_array, 2>{ // 32x32x36
 double3_array{ -16, -16, -18 },
								  double3_array{ 16, 16, 18 } }
};
