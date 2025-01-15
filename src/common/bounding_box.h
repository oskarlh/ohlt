#pragma once

#include "cmdlib.h" //--vluzacn


struct bounding_box {
    double3_array mins;
    double3_array maxs;
};

constexpr bounding_box empty_bounding_box {
    .mins = double3_array{ 999999999.999, 999999999.999, 999999999.999 },
    .maxs = double3_array{ -999999999.999, -999999999.999, -999999999.999 }
};


enum bounding_box_state
{
    disjoint,      // neither boxes touch
    in_union,         // this box intersects with the other box
    subset,        // this box is inside the other box
    superset,       // this box is completly envelops the other box
};


// Tests if other box is completely outside of this box
bool test_disjoint(const bounding_box& thisBox, const bounding_box& other) noexcept;

// Returns true if this box is completely inside the other box
bool test_subset(const bounding_box& thisBox, const bounding_box& otherBox) noexcept;

// Returns true if this box contains the other box completely
bool test_superset(const bounding_box& thisBox, const bounding_box& otherBox) noexcept;

// Returns true if this box partially intersects the other box
bool test_union(const bounding_box& thisBox, const bounding_box& otherBox) noexcept;

bounding_box_state test_all(const bounding_box& thisBox, const bounding_box& otherBox) noexcept;



void set_bounding_box(bounding_box& thisBox, const double3_array& maxs, const double3_array& mins) noexcept;

void set_bounding_box(bounding_box& thisBox, const bounding_box& otherBox) noexcept;

void add_to_bounding_box(bounding_box& thisBox, const double3_array& point) noexcept;
void add_to_bounding_box(bounding_box& thisBox, const float3_array& point) noexcept;
void add_to_bounding_box(bounding_box& thisBox, bounding_box& other) noexcept;
