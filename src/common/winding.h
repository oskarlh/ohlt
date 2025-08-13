#pragma once

#include "bounding_box.h"
#include "bspfile.h"
#include "mathlib.h"
#include "planes.h"

enum class face_side {
	front = 0,
	back = 1,
	on = 2,
	cross = 3
};

template <std::floating_point VecElement>
class winding_base final {
  public:
	using vec_element = VecElement;
	using vec3 = std::array<vec_element, 3>;

  private:
	using points_vector = usually_inplace_vector<vec3, 18>;
	points_vector m_Points;

  public:
	// General Functions
	void Print() const;
	void getPlane(dplane_t& plane) const;
	void getPlane(mapplane_t& plane) const;
	vec_element getArea() const;
	bounding_box getBounds() const;
	vec3 getCenter() const noexcept;
	void Check(vec_element epsilon = ON_EPSILON)
		const;			// Developer check for validity
	bool Valid() const; // Runtime/user/normal check for validity
	bool empty() const;

	inline operator bool() const {
		return !empty();
	}

	void clear();
	void shrink_to_fit();

	void pushPoint(vec3 const & newpoint);
	std::size_t size() const;

	// Specialized Functions
	void RemoveColinearPoints(vec_element epsilon = ON_EPSILON);
	bool mutating_clip(
		vec3 const & planeNormal,
		vec_element planeDist,
		bool keepon,
		vec_element epsilon = ON_EPSILON
	);
	void Clip(
		vec3 const & normal,
		vec_element planeDist,
		winding_base& front,
		winding_base& back,
		vec_element epsilon = ON_EPSILON
	) const;
	bool Chop(
		vec3 const & normal,
		vec_element planeDist,
		vec_element epsilon = ON_EPSILON
	);

	void clip(
		vec3 const & dividingPlaneNormal,
		vec_element dividingPlaneDist,
		winding_base& back,
		winding_base& front,
		std::optional<vec_element> distOverrideForFuncDetail = std::nullopt,
		vec_element epsilon = ON_EPSILON
	) const;
	void clip(
		mapplane_t const & dividingPlane,
		winding_base& back,
		winding_base& front,
		std::optional<vec_element> distOverrideForFuncDetail = std::nullopt,
		vec_element epsilon = ON_EPSILON
	) const;

	face_side WindingOnPlaneSide(
		vec3 const & normal,
		vec_element planeDist,
		vec_element epsilon = ON_EPSILON
	) const;

  private:
	void getPlane(vec3& normal, vec_element& dist) const;

  public:
	// Construction
	winding_base();								// Do nothing :)
	winding_base(std::span<vec3 const> points); // Create from raw points
	winding_base(dface_t const & face, vec_element epsilon = ON_EPSILON);
	winding_base(dplane_t const & face);
	winding_base(mapplane_t const & face);
	winding_base(vec3 const & normal, vec_element dist);
	winding_base(std::size_t points);
	winding_base(winding_base const & other);
	winding_base(winding_base&& other);
	~winding_base();
	winding_base& operator=(winding_base const & other);
	winding_base& operator=(winding_base&& other);

	// Misc

  private:
	void initFromPlane(vec3 const & normal, vec_element planeDist);

  public:
	inline std::span<vec3 const> points() const noexcept {
		return { m_Points };
	}

	inline std::size_t point_count() const noexcept {
		return m_Points.size();
	}

	// Precondition: index < point_count()
	inline vec3 const & point(std::size_t index) const noexcept {
		return m_Points[index];
	}

	// Precondition: index < point_count()
	inline vec3 const &
	point_before(std::size_t index, std::size_t nBefore) const noexcept {
		std::ptrdiff_t i = std::ptrdiff_t(index - nBefore);
		while (i < 0) {
			i = point_count();
		}
		return m_Points[i];
	}

	// Precondition: index < point_count()
	inline vec3 const &
	point_after(std::size_t index, std::size_t nAfter) const noexcept {
		return m_Points[(index + nAfter) % point_count()];
	}

	// Precondition: index < point_count()
	inline vec3&
	replace_point(std::size_t index, vec3 const & newPoint) noexcept {
		m_Points[index] = newPoint;
		return m_Points[index];
	}

	inline void add_offset_to_points(vec3 const & offset) noexcept {
		for (vec3& p : m_Points) {
			p = vector_add(p, offset);
		}
	}

	inline vec3& push_point(vec3 const & newPoint) noexcept {
		return m_Points.emplace_back(newPoint);
	}

	inline void reverse_points() noexcept {
		std::ranges::reverse(m_Points);
	}

	inline void reserve_point_storage(std::size_t numPoints) noexcept {
		m_Points.reserve(numPoints);
	}

  public:
	friend inline void swap(winding_base& a, winding_base& b) {
		using std::swap;
		swap(a.m_Points, b.m_Points);
	}
};

namespace std {
	template <class VT>
	struct hash<winding_base<VT>> {
		constexpr std::size_t operator()(winding_base<VT> const & winding
		) const noexcept {
			return hash_multiple(winding.points());
		}
	};
} // namespace std

using accurate_winding = winding_base<double>;
using fast_winding = winding_base<float>;
