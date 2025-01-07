#pragma once

#include "mathtypes.h"
#include "mathlib.h"


#define TRCopy(a,b) { (b)[0]=(a)[0]; (b)[1]=(a)[1]; (b)[2]=(a)[2]; }

class mut_vec3_arg {
	bool useArray = true;
	vec3_array copyOfDataA;
	vec3_t copyOfDataB;
	vec_t* out;

public:
	mut_vec3_arg(vec3_t& arg) {
		TRCopy(arg, copyOfDataA);
		TRCopy(arg, copyOfDataB);
		out = arg;
	}
	mut_vec3_arg(vec3_array& arg): copyOfDataA(arg) {
		TRCopy(arg, copyOfDataA);
		TRCopy(arg, copyOfDataB);
		out = arg.data();
	}

	~mut_vec3_arg() {
		if(useArray) {
			TRCopy(copyOfDataA, out);
		} else {
			TRCopy(copyOfDataB, out);
		}
	}



	operator vec3_array&() {
		useArray = true;
		return copyOfDataA;
	}
	operator vec3_t&() {
		useArray = false;
		return copyOfDataB;
	}

};



class const_vec3_arg {
	vec3_array copyOfDataA;
	vec3_t copyOfDataB;

public:
	constexpr const_vec3_arg(const vec3_t arg) {
		TRCopy(arg, copyOfDataA);
		TRCopy(arg, copyOfDataB);
	}
	constexpr const_vec3_arg(const vec3_array& arg): copyOfDataA(arg) {
		TRCopy(arg, copyOfDataA);
		TRCopy(arg, copyOfDataB);
	}


	constexpr operator const vec3_array&() {
		return copyOfDataA;
	}
	constexpr operator const vec3_t&() {
		return copyOfDataB;
	}

};


inline const_vec3_arg vec3_arg(const vec3_t arg) {
	return const_vec3_arg(arg);
}

inline const_vec3_arg vec3_arg(const vec3_array& arg) {
	return const_vec3_arg(arg);
}

inline mut_vec3_arg vec3_arg(vec3_t& arg) {
	return mut_vec3_arg(arg);
}

inline mut_vec3_arg vec3_arg(vec3_array& arg) {
	return mut_vec3_arg(arg);
}





