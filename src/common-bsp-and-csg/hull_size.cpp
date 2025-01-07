#include "hull_size.h"

const hull_sizes standard_hull_sizes{
    std::array<vec3_array, 2>{
        // 0x0x0
        vec3_array{0, 0, 0},
        vec3_array{0, 0, 0}
    }
    ,
    std::array<vec3_array, 2>{
        // 32x32x72
        vec3_array{-16, -16, -36},
        vec3_array{16, 16, 36}
    }
    ,                                                      
    std::array<vec3_array, 2>{
        // 64x64x64
        vec3_array{-32, -32, -32},
        vec3_array{32, 32, 32}
    }
    ,                                                      
    std::array<vec3_array, 2>{
        // 32x32x36
        vec3_array{-16, -16, -18},
        vec3_array{16, 16, 18}
    }                                                     
};
