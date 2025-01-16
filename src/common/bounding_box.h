#pragma once

#include "cmdlib.h" //--vluzacn

struct bounding_box {
	double3_array mins;
	double3_array maxs;
};

constexpr bounding_box empty_bounding_box{
	.mins = double3_array{  999999999.999,  999999999.999,	 999999999.999 },
	.maxs = double3_array{ -999999999.999, -999999999.999, -999999999.999 }
};

enum bounding_box_state {
	disjoint, // neither boxes touch
	in_union, // this box intersects with the other box
	subset,	  // this box is inside the other box
	superset, // this box is completly envelops the other box
};

// Tests if other box is completely outside of this box
bool test_disjoint(bounding_box const & thisBox, bounding_box const & other)
	noexcept;

// Returns true if this box is completely inside the other box
bool test_subset(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept;

// Returns true if this box contains the other box completely
bool test_superset(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept;

// Returns true if this box partially intersects the other box
bool test_union(bounding_box const & thisBox, bounding_box const & otherBox)
	noexcept;

bounding_box_state test_all(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept;

void set_bounding_box(
	bounding_box& thisBox, double3_array const & maxs,
	double3_array const & mins
) noexcept;

void set_bounding_box(bounding_box& thisBox, bounding_box const & otherBox)
	noexcept;

void add_to_bounding_box(bounding_box& thisBox, double3_array const & point)
	noexcept;
void add_to_bounding_box(bounding_box& thisBox, float3_array const & point)
	noexcept;
void add_to_bounding_box(bounding_box& thisBox, bounding_box& other)
	noexcept;
