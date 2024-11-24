#pragma once

#include "cmdlib.h" //--vluzacn

typedef unsigned char byte;

#ifdef DOUBLEVEC_T
typedef double vec_t;
#else
typedef float vec_t;
#endif
typedef vec_t   vec3_t[3];                                 // x,y,z
