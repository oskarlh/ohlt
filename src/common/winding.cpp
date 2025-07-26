#include "winding.h"

#include "cmdlib.h"
#include "hlassert.h"
#include "log.h"
#include "mathlib.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <span>

constexpr float bogus_range = 80000.0f;

//
// winding_base Public Methods
//

template <std::floating_point VecElement>
void winding_base<VecElement>::Print() const {
	for (vec3 const & point : m_Points) {
		Log("(%5.2f, %5.2f, %5.2f)\n", point[0], point[1], point[2]);
	}
}

template <std::floating_point VecElement>
void winding_base<VecElement>::getPlane(dplane_t& plane) const {
	if (size() < 3) {
		plane.normal = {};
		plane.dist = 0.0;
		return;
	}

	vec3 plane_normal = cross_product(
		vector_subtract(m_Points[0], m_Points[1]),
		vector_subtract(m_Points[2], m_Points[1])
	);
	normalize_vector(plane_normal);
	plane.normal = to_float3(plane_normal);
	plane.dist = dot_product(m_Points[0], plane.normal);
}

template <std::floating_point VecElement>
void winding_base<VecElement>::getPlane(mapplane_t& plane) const {
	if (size() < 3) {
		plane.normal = {};
		plane.dist = 0.0;
		return;
	}

	vec3 plane_normal = cross_product(
		vector_subtract(m_Points[0], m_Points[1]),
		vector_subtract(m_Points[2], m_Points[1])
	);
	normalize_vector(plane_normal);
	plane.normal = to_double3(plane_normal);
	plane.dist = dot_product(m_Points[0], plane.normal);
}

template <std::floating_point VecElement>
void winding_base<VecElement>::getPlane(vec3& normal, vec_element& dist)
	const {
	if (size() < 3) {
		normal = {};
		dist = 0.0;
		return;
	}

	normal = cross_product(
		vector_subtract(m_Points[2], m_Points[0]),
		vector_subtract(m_Points[1], m_Points[0])
	);
	normalize_vector(normal);
	dist = dot_product(m_Points[0], normal);
}

template <std::floating_point VecElement>
auto winding_base<VecElement>::getArea() const -> vec_element {
	if (size() < 3) {
		return 0.0;
	}

	vec_element total = 0.0;
	for (std::size_t i = 2; i < size(); ++i) {
		vec3 const cross = cross_product(
			vector_subtract(m_Points[i - 1], m_Points[0]),
			vector_subtract(m_Points[i], m_Points[0])
		);
		total += 0.5 * vector_length(cross);
	}
	return total;
}

template <std::floating_point VecElement>
bounding_box winding_base<VecElement>::getBounds() const {
	bounding_box bounds = empty_bounding_box;
	for (vec3 const & point : m_Points) {
		add_to_bounding_box(bounds, point);
	}
	return bounds;
}

template <std::floating_point VecElement>
auto winding_base<VecElement>::getCenter() const noexcept -> vec3 {
	winding_base<VecElement>::vec3 center{};

	for (vec3 const & point : m_Points) {
		center = vector_add(point, center);
	}

	vec_element const scale = vec_element(1.0) / std::max(1UZ, size());
	return vector_scale(center, scale);
}

template <std::floating_point VecElement>
void winding_base<VecElement>::Check(vec_element epsilon) const {
	vec_element d, edgedist;
	vec3 dir, edgenormal, facenormal;

	if (size() < 3) {
		Error("winding_base::Check : %zu points", size());
	}

	vec_element const area = getArea();
	if (area < 1) {
		Error("winding_base::Check : %f area", area);
	}

	vec_element facedist;
	getPlane(facenormal, facedist);

	for (std::size_t i = 0; i < size(); ++i) {
		vec3 const & p1 = m_Points[i];

		for (vec_element p1Element : p1) {
			if (p1Element > bogus_range || p1Element < -bogus_range) {
				Error("winding_base::Check : bogus_range: %f", p1Element);
			}
		}

		std::size_t j = i + 1 == size() ? 0 : i + 1;

		// check the point is on the face plane
		d = dot_product(p1, facenormal) - facedist;
		if (d < -epsilon || d > epsilon) {
			Error("winding_base::Check : point off plane");
		}

		// check the edge isn't degenerate
		vec3 const & p2 = m_Points[j];
		dir = vector_subtract(p2, p1);

		if (vector_length(dir) < epsilon) {
			Error("winding_base::Check : degenerate edge");
		}

		edgenormal = cross_product(facenormal, dir);
		normalize_vector(edgenormal);
		edgedist = dot_product(p1, edgenormal);
		edgedist += epsilon;

		// all other points must be on front side
		for (std::size_t j = 0; j < size(); ++j) {
			if (j == i) {
				continue;
			}
			d = dot_product(m_Points[j], edgenormal);
			if (d > edgedist) {
				Error("winding_base::Check : non-convex");
			}
		}
	}
}

template <std::floating_point VecElement>
bool winding_base<VecElement>::Valid() const {
	// TODO: Check to ensure there are 3 non-colinear points
	return size() >= 3;
}

template <std::floating_point VecElement>
bool winding_base<VecElement>::empty() const {
	return m_Points.empty();
}

template <std::floating_point VecElement>
void winding_base<VecElement>::clear(bool shrinkToFit) {
	m_Points.clear();
	m_Points.shrink_to_fit();
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base() { }

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(
	vec3 const * points, std::size_t numpoints
) {
	hlassert(numpoints >= 3);
	m_Points.assign_range(std::span(points, numpoints));
}

template <std::floating_point VecElement>
auto winding_base<VecElement>::operator=(winding_base const & other
) -> winding_base& {
	m_Points = other.m_Points;
	return *this;
}

template <std::floating_point VecElement>
auto winding_base<VecElement>::operator=(winding_base&& other
) -> winding_base& {
	m_Points = std::move(other.m_Points);
	return *this;
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(std::size_t numpoints) {
	hlassert(numpoints >= 3);
	m_Points.resize(numpoints);
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(winding_base const & other) {
	m_Points = other.m_Points;
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(winding_base&& other) :
	m_Points(std::move(other.m_Points)) { }

template <std::floating_point VecElement>
winding_base<VecElement>::~winding_base() { }

template <std::floating_point VecElement>
void winding_base<VecElement>::initFromPlane(
	vec3 const & normal, vec_element const dist
) {
	vec_element max, v;

	// find the major axis

	max = -bogus_range;
	std::size_t majorAxis{ -1zu };
	for (std::size_t i = 0; i < 3; i++) {
		v = fabs(normal[i]);
		if (v > max) {
			max = v;
			majorAxis = i;
		}
	}
	if (majorAxis == -1zu) {
		Error("winding_base::initFromPlane no major axis found\n");
	}

	vec3 vup{};
	switch (majorAxis) {
		case 0:
		case 1:
			vup[2] = 1;
			break;
		case 2:
			vup[0] = 1;
			break;
	}

	v = dot_product(vup, normal);
	vup = vector_fma(normal, -v, vup);
	normalize_vector(vup);

	vec3 org = vector_scale(normal, dist);

	vec3 vright = cross_product(vup, normal);

	vup = vector_scale(vup, bogus_range);
	vright = vector_scale(vright, bogus_range);

	// Project a really big axis-aligned box onto the plane
	m_Points.resize(4);
	m_Points[0] = vector_add(vector_subtract(org, vright), vup);
	m_Points[1] = vector_add(vector_add(org, vright), vup);
	m_Points[2] = vector_subtract(vector_add(org, vright), vup);
	m_Points[3] = vector_subtract(vector_subtract(org, vright), vup);
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(
	vec3 const & normal, vec_element const dist
) {
	initFromPlane(normal, dist);
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(
	dface_t const & face, vec_element epsilon
) {
	dvertex_t* dv;

	m_Points.resize(face.numedges);

	for (std::size_t i = 0; i < face.numedges; i++) {
		std::int32_t se = g_dsurfedges[face.firstedge + i];
		std::uint16_t v;
		if (se < 0) {
			v = g_dedges[-se].v[1];
		} else {
			v = g_dedges[se].v[0];
		}

		dv = &g_dvertexes[v];
		m_Points[i] = to_vec3<vec_element>(dv->point);
	}

	RemoveColinearPoints(epsilon);
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(dplane_t const & plane) {
	initFromPlane(to_vec3<vec_element>(plane.normal), plane.dist);
}

template <std::floating_point VecElement>
winding_base<VecElement>::winding_base(mapplane_t const & plane) {
	initFromPlane(to_vec3<vec_element>(plane.normal), plane.dist);
}

// Remove the colinear point of any three points that form a triangle which
// is thinner than epsilon
template <std::floating_point VecElement>
void winding_base<VecElement>::RemoveColinearPoints(vec_element epsilon) {
	for (std::size_t i = 0; i < size(); ++i) {
		vec3 const & p1 = m_Points[(i + size() - 1) % size()];
		vec3 const & p2 = m_Points[i];
		vec3 const & p3 = m_Points[(i + 1) % size()];
		vec3 const v1 = vector_subtract(p2, p1);
		vec3 const v2 = vector_subtract(p3, p2);
		// v1 or v2 might be close to 0
		if (dot_product(v1, v2) * dot_product(v1, v2)
			>= dot_product(v1, v1) * dot_product(v2, v2)
				- epsilon * epsilon
					* (dot_product(v1, v1) + dot_product(v2, v2)
					   + epsilon * epsilon))
		// v2 == k * v1 + v3 && abs (v3) < epsilon || v1 == k * v2 + v3 &&
		// abs (v3) < epsilon
		{
			for (; i < size() - 1; i++) {
				m_Points[i] = m_Points[i + 1];
			}
			m_Points.pop_back();
			i = -1;
			continue;
		}
	}
}

template <std::floating_point VecElement>
void winding_base<VecElement>::Clip(
	dplane_t const & plane,
	winding_base& front,
	winding_base& back,
	vec_element epsilon
) const {
	Clip(
		to_vec3<vec_element>(plane.normal), plane.dist, front, back, epsilon
	);
}

template <std::floating_point VecElement>
void winding_base<VecElement>::Clip(
	vec3 const & normal,
	vec_element dist,
	winding_base& front,
	winding_base& back,
	vec_element epsilon
) const {
	auto dists = std::make_unique_for_overwrite<vec_element[]>(size() + 1);
	auto sides = std::make_unique_for_overwrite<face_side[]>(size() + 1);
	std::array<std::size_t, 3> counts{};

	// determine sides for each point
	for (std::size_t i = 0; i < size(); i++) {
		vec_element dot = dot_product(m_Points[i], normal);
		dot -= dist;
		dists[i] = dot;
		if (dot > epsilon) {
			sides[i] = face_side::front;
		} else if (dot < -epsilon) {
			sides[i] = face_side::back;
		} else {
			sides[i] = face_side::on;
		}
		counts[(std::size_t) sides[i]]++;
	}
	sides[size()] = sides[0];
	dists[size()] = dists[0];

	front.clear();
	back.clear();
	if (!counts[(std::size_t) face_side::front]) {
		back = *this;
		return;
	}
	if (!counts[(std::size_t) face_side::back]) {
		front = *this;
		return;
	}

	front.m_Points.reserve(
		size() + 4
	); // Optimization only - can safely be removed
	back.m_Points.reserve(
		size() + 4
	); // Optimization only - can safely be removed

	for (std::size_t i = 0; i < size(); ++i) {
		vec3 const & p1 = m_Points[i];

		if (sides[i] == face_side::on) {
			front.m_Points.emplace_back(p1);
			back.m_Points.emplace_back(p1);
			continue;
		} else if (sides[i] == face_side::front) {
			front.m_Points.emplace_back(p1);
		} else if (sides[i] == face_side::back) {
			back.m_Points.emplace_back(p1);
		}

		if ((sides[i + 1] == face_side::on)
			| (sides[i + 1] == sides[i]
			)) // | instead of || for branch optimization
		{
			continue;
		}

		// generate a split point
		vec3 mid;
		std::size_t tmp = i + 1;
		if (tmp >= size()) {
			tmp = 0;
		}
		vec3 const & p2 = m_Points[tmp];
		vec_element dot = dists[i] / (dists[i] - dists[i + 1]);

		for (std::size_t j = 0; j < 3;
			 j++) { // avoid round off error when possible
			if (normal[j] == 1) {
				mid[j] = dist;
			} else if (normal[j] == -1) {
				mid[j] = -dist;
			} else {
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
			}
		}

		front.m_Points.emplace_back(mid);
		back.m_Points.emplace_back(mid);
	}

	front.RemoveColinearPoints(epsilon);
	back.RemoveColinearPoints(epsilon);
}

template <std::floating_point VecElement>
bool winding_base<VecElement>::Chop(
	vec3 const & normal, vec_element const dist, vec_element epsilon
) {
	winding_base f;
	winding_base b;

	Clip(normal, dist, f, b, epsilon);
	swap(*this, f);
	return !empty();
}

template <std::floating_point VecElement>
face_side winding_base<VecElement>::WindingOnPlaneSide(
	vec3 const & normal, vec_element const dist, vec_element epsilon
) {
	bool front = false;
	bool back = false;
	for (std::size_t i = 0; i < size(); i++) {
		vec_element d = dot_product(m_Points[i], normal) - dist;
		if (d < -epsilon) {
			if (front) {
				return face_side::cross;
			}
			back = true;
			continue;
		}
		if (d > epsilon) {
			if (back) {
				return face_side::cross;
			}
			front = true;
			continue;
		}
	}

	if (back) {
		return face_side::back;
	}
	if (front) {
		return face_side::front;
	}
	return face_side::on;
}

template <std::floating_point VecElement>
bool winding_base<VecElement>::mutating_clip(
	vec3 const & planeNormal,
	vec_element planeDist,
	bool keepon,
	vec_element epsilon
) {
	auto dists = std::make_unique_for_overwrite<vec_element[]>(size() + 1);
	auto sides = std::make_unique_for_overwrite<face_side[]>(size() + 1);
	std::size_t counts[3];
	vec_element dot;

	counts[0] = counts[1] = counts[2] = 0;

	// determine sides for each point
	// do this exactly, with no epsilon so tiny portals still work
	for (std::size_t i = 0; i < size(); ++i) {
		dot = dot_product(m_Points[i], planeNormal);
		dot -= planeDist;
		dists[i] = dot;
		if (dot > epsilon) {
			sides[i] = face_side::front;
		} else if (dot < -epsilon) {
			sides[i] = face_side::back;
		} else {
			sides[i] = face_side::on;
		}
		++counts[(std::size_t) sides[i]];
	}
	sides[size()] = sides[0];
	dists[size()] = dists[0];

	if (keepon && !counts[0] && !counts[1]) {
		return true;
	}

	if (!counts[0]) {
		m_Points.clear();
		m_Points.shrink_to_fit();
		return false;
	}

	if (!counts[1]) {
		return true;
	}

	points_vector newPoints{};
	newPoints.reserve(size() + 4);

	for (std::size_t i = 0; i < size(); ++i) {
		vec3 const & p1 = m_Points[i];

		if (sides[i] == face_side::on) {
			newPoints.emplace_back(p1);
			continue;
		} else if (sides[i] == face_side::front) {
			newPoints.emplace_back(p1);
		}

		if (sides[i + 1] == face_side::on || sides[i + 1] == sides[i]) {
			continue;
		}

		// generate a split point
		vec3 mid;
		std::size_t tmp = i + 1;
		if (tmp >= size()) {
			tmp = 0;
		}
		vec3 const & p2 = m_Points[tmp];
		dot = dists[i] / (dists[i] - dists[i + 1]);
		for (std::size_t j = 0; j < 3;
			 ++j) { // avoid round off error when possible
			if (planeNormal[j] == 1) {
				mid[j] = planeDist;
			} else if (planeNormal[j] == -1) {
				mid[j] = -planeDist;
			} else {
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
			}
		}
		newPoints.emplace_back(mid);
	}

	using std::swap;
	swap(m_Points, newPoints);

	RemoveColinearPoints(epsilon);

	if (m_Points.empty()) {
		m_Points.shrink_to_fit();
		return false;
	}

	return true;
}

/*
 * ==================
 * Divide
 * Divides a winding by a plane, producing one or two windings.  The
 * original winding is not damaged or freed.  If only on one side, the
 * returned winding will be the input winding.  If on both sides, two
 * new windings will be created.
 * ==================
 */
template <std::floating_point VecElement>
winding_base<VecElement>::division_result winding_base<VecElement>::Divide(
	mapplane_t const & split, vec_element epsilon
) const {
	auto dists = std::make_unique_for_overwrite<vec_element[]>(size() + 1);
	auto sides = std::make_unique_for_overwrite<face_side[]>(size() + 1);
	std::array<std::size_t, 3> counts{ 0, 0, 0 };

	vec_element dotSum = 0;

	// determine sides for each point
	for (std::size_t i = 0; i < size(); i++) {
		vec_element dot = dot_product(m_Points[i], split.normal);
		dot -= split.dist;
		dotSum += dot;
		dists[i] = dot;

		face_side side = face_side::on;
		if (dot > epsilon) {
			side = face_side::front;
		} else if (dot < -epsilon) {
			side = face_side::back;
		}
		sides[i] = side;
		counts[(std::size_t) side]++;
	}
	sides[size()] = sides[0];
	dists[size()] = dists[0];

	if (!counts[(std::size_t) face_side::back]) {
		if (counts[(std::size_t) face_side::front]) {
			return one_sided_division_result::all_in_the_front;
		}
		if (dotSum > NORMAL_EPSILON) {
			return one_sided_division_result::all_in_the_front;
		}
		return one_sided_division_result::all_in_the_back;
	}
	if (!counts[(std::size_t) face_side::front]) {
		return one_sided_division_result::all_in_the_back;
	}

	winding_base back;
	winding_base front;
	back.m_Points.reserve(size() + 4);
	front.m_Points.reserve(size() + 4);

	for (std::size_t i = 0; i < size(); i++) {
		vec3 const & p1 = m_Points[i];

		if (sides[i] == face_side::on) {
			front.m_Points.emplace_back(p1);
			back.m_Points.emplace_back(p1);
			continue;
		} else if (sides[i] == face_side::front) {
			front.m_Points.emplace_back(p1);
		} else if (sides[i] == face_side::back) {
			back.m_Points.emplace_back(p1);
		}

		if (sides[i + 1] == face_side::on || sides[i + 1] == sides[i]) {
			continue;
		}

		// generate a split point
		vec3 mid;
		std::size_t tmp = i + 1;
		if (tmp >= size()) {
			tmp = 0;
		}
		vec3 const & p2 = m_Points[tmp];
		vec_element dot = dists[i] / (dists[i] - dists[i + 1]);
		for (std::size_t j = 0; j < 3;
			 j++) { // avoid round off error when possible
			if (split.normal[j] == 1) {
				mid[j] = split.dist;
			} else if (split.normal[j] == -1) {
				mid[j] = -split.dist;
			} else {
				mid[j] = p1[j] + dot * (p2[j] - p1[j]);
			}
		}

		front.m_Points.emplace_back(mid);
		back.m_Points.emplace_back(mid);
	}

	front.RemoveColinearPoints(epsilon);
	back.RemoveColinearPoints(epsilon);

	if (!front) {
		return one_sided_division_result::all_in_the_back;
	}
	if (!back) {
		return one_sided_division_result::all_in_the_front;
	}
	return split_division_result{ .back = std::move(back),
								  .front = std::move(front) };
}

// Unused??
template <std::floating_point VecElement>
void winding_base<VecElement>::pushPoint(vec3 const & newpoint) {
	m_Points.emplace_back(newpoint);
}

template <std::floating_point VecElement>
std::size_t winding_base<VecElement>::size() const {
	return m_Points.size();
}

template class winding_base<float>;
template class winding_base<double>;
