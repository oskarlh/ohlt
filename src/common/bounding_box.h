#pragma once

#include "cmdlib.h" //--vluzacn


struct bounding_box {
    vec3_array maxs = { -999999999.999, -999999999.999, -999999999.999 };
    vec3_array mins = { 999999999.999, 999999999.999, 999999999.999 };
};


enum bounding_box_state
{
    disjoint,      // neither boxes touch
    in_union,         // this box intersects with the other box
    subset,        // this box is inside the other box
    superset,       // this box is completly envelops the other box
};


// Tests if other box is completely outside of this box
bool test_disjoint(const bounding_box& thisBox, const bounding_box& other);

// Returns true if this box is completely inside the other box
bool test_subset(const bounding_box& thisBox, const bounding_box& otherBox);

// Returns true if this box contains the other box completely
bool test_superset(const bounding_box& thisBox, const bounding_box& otherBox);

// Returns true if this box partially intersects the other box
bool test_union(const bounding_box& thisBox, const bounding_box& otherBox);

bounding_box_state test(const bounding_box& thisBox, const bounding_box& otherBox);



void set_bounding_box(bounding_box& thisBox, const vec3_array& maxs, const vec3_array& mins);

void set_bounding_box(bounding_box& thisBox, const bounding_box& otherBox);

void add_to_bounding_box(bounding_box& thisBox, const vec3_array& point);
void add_to_bounding_box(bounding_box& thisBox, bounding_box& other);
