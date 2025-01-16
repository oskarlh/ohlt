#include "./bounding_box.h"

#include "mathlib.h"

#include <algorithm>

// Tests if other box is completely outside of this box
bool test_disjoint(bounding_box const & thisBox, bounding_box const & other)
	noexcept {
	return (thisBox.mins[0] > other.maxs[0] + ON_EPSILON)
		|| (thisBox.mins[1] > other.maxs[1] + ON_EPSILON)
		|| (thisBox.mins[2] > other.maxs[2] + ON_EPSILON)
		|| (thisBox.maxs[0] < other.mins[0] - ON_EPSILON)
		|| (thisBox.maxs[1] < other.mins[1] - ON_EPSILON)
		|| (thisBox.maxs[2] < other.mins[2] - ON_EPSILON);
}

// Returns true if this box is completely inside the other box
bool test_subset(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept {
	return (thisBox.mins[0] >= otherBox.mins[0])
		&& (thisBox.maxs[0] <= otherBox.maxs[0])
		&& (thisBox.mins[1] >= otherBox.mins[1])
		&& (thisBox.maxs[1] <= otherBox.maxs[1])
		&& (thisBox.mins[2] >= otherBox.mins[2])
		&& (thisBox.maxs[2] <= otherBox.maxs[2]);
}

// Returns true if this box contains the other box completely
bool test_superset(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept {
	return test_subset(otherBox, thisBox);
}

// Returns true if this box partially intersects the other box
bool test_union(bounding_box const & thisBox, bounding_box const & otherBox)
	noexcept {
	bounding_box tempBox;
	tempBox.mins[0] = std::max(thisBox.mins[0], otherBox.mins[0]);
	tempBox.mins[1] = std::max(thisBox.mins[1], otherBox.mins[1]);
	tempBox.mins[2] = std::max(thisBox.mins[2], otherBox.mins[2]);
	tempBox.maxs[0] = std::min(thisBox.maxs[0], otherBox.maxs[0]);
	tempBox.maxs[1] = std::min(thisBox.maxs[1], otherBox.maxs[1]);
	tempBox.maxs[2] = std::min(thisBox.maxs[2], otherBox.maxs[2]);

	return !(
		(tempBox.mins[0] > tempBox.maxs[0])
		|| (tempBox.mins[1] > tempBox.maxs[1])
		|| (tempBox.mins[2] > tempBox.maxs[2])
	);
}

bounding_box_state test_all(
	bounding_box const & thisBox, bounding_box const & otherBox
) noexcept {
	if (test_disjoint(thisBox, otherBox)) {
		return disjoint;
	}
	if (test_subset(thisBox, otherBox)) {
		return subset;
	}
	if (test_superset(thisBox, otherBox)) {
		return superset;
	}
	return in_union;
}

void set_bounding_box(
	bounding_box& thisBox,
	double3_array const & maxs,
	double3_array const & mins
) noexcept {
	thisBox.maxs = maxs;
	thisBox.mins = mins;
}

void set_bounding_box(bounding_box& thisBox, bounding_box const & otherBox)
	noexcept {
	set_bounding_box(thisBox, otherBox.maxs, otherBox.mins);
}

void add_to_bounding_box(bounding_box& thisBox, double3_array const & point)
	noexcept {
	thisBox.mins[0] = std::min(thisBox.mins[0], point[0]);
	thisBox.maxs[0] = std::max(thisBox.maxs[0], point[0]);
	thisBox.mins[1] = std::min(thisBox.mins[1], point[1]);
	thisBox.maxs[1] = std::max(thisBox.maxs[1], point[1]);
	thisBox.mins[2] = std::min(thisBox.mins[2], point[2]);
	thisBox.maxs[2] = std::max(thisBox.maxs[2], point[2]);
}

void add_to_bounding_box(bounding_box& thisBox, float3_array const & point)
	noexcept {
	add_to_bounding_box(thisBox, to_double3(point));
}

void add_to_bounding_box(bounding_box& thisBox, bounding_box& other)
	noexcept {
	add_to_bounding_box(thisBox, other.maxs);
	add_to_bounding_box(thisBox, other.mins);
}
