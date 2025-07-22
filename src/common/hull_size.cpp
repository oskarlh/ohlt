#include "hull_size.h"

hull_sizes const standard_hull_sizes{
	// 0x0x0
	std::array<float3_array, 2>{ float3_array{ 0, 0, 0 },
								 float3_array{ 0, 0, 0 } },
	// 32x32x72
	std::array<float3_array, 2>{ float3_array{ -16, -16, -36 },
								 float3_array{ 16, 16, 36 } },
	// 64x64x64
	std::array<float3_array, 2>{ float3_array{ -32, -32, -32 },
								 float3_array{ 32, 32, 32 } },
	// 32x32x36
	std::array<float3_array, 2>{ float3_array{ -16, -16, -18 },
								 float3_array{ 16, 16, 18 } }
};
