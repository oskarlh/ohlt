#include "winding.h"

#include "cmdlib.h"
#include "log.h"
#include "mathlib.h"
#include "hlassert.h"

#include <algorithm>
#include <span>

constexpr vec_t bogus_range = 80000.0;

//
// Winding Public Methods
//

void Winding::Print() const
{
    std::uint_least32_t x;

    for (const vec3_array& point : m_Points)
    {
        Log("(%5.2f, %5.2f, %5.2f)\n", point[0], point[1], point[2]);
    }
}

void Winding::getPlane(dplane_t& plane) const
{
    vec3_array          v1, v2;
    vec3_array          plane_normal;

    if (size() < 3)
    {
        VectorClear(plane.normal);
        plane.dist = 0.0;
        return;
    }

    VectorSubtract(m_Points[2], m_Points[1], v1);
    VectorSubtract(m_Points[0], m_Points[1], v2);

    CrossProduct(v2, v1, plane_normal);
    VectorNormalize(plane_normal);
    VectorCopy(plane_normal, plane.normal);               // change from vec_t
    plane.dist = DotProduct(m_Points[0], plane.normal);
}

void Winding::getPlane(vec3_array& normal, vec_t& dist) const {
    vec3_array v1, v2;

    if (size() < 3)
    {
        normal.fill(0.0);
        dist = 0.0;
        return;
    }

    VectorSubtract(m_Points[1], m_Points[0], v1);
    VectorSubtract(m_Points[2], m_Points[0], v2);
    CrossProduct(v2, v1, normal);
    VectorNormalize(normal);
    dist = DotProduct(m_Points[0], normal);
}

vec_t Winding::getArea() const {
    if (size() < 3)
    {
        return 0.0;
    }

    vec_t total = 0.0;
    for (std::size_t i = 2; i < size(); ++i)
    {
        vec3_array d1, d2, cross;
        VectorSubtract(m_Points[i - 1], m_Points[0], d1);
        VectorSubtract(m_Points[i], m_Points[0], d2);
        CrossProduct(d1, d2, cross);
        total += 0.5 * VectorLength(cross);
    }
    return total;
}

bounding_box Winding::getBounds() const {
    bounding_box bounds{};
    for (const vec3_array& point : m_Points)
    {
        add_to_bounding_box(bounds, point);
    }
    return bounds;
}

vec3_array Winding::getCenter() const
{
    vec3_array center{};

    for (const vec3_array& point : m_Points)
    {
        VectorAdd(point, center, center);
    }

    vec_t scale = vec_t(1.0) / std::max(1UZ, size());
    VectorScale(center, scale, center);
    return center;
}

void Winding::Check(vec_t epsilon) const
{
    unsigned int i, j;
    vec_t d, edgedist;
    vec3_array dir, edgenormal, facenormal;
    vec_t area;
    vec_t facedist;

    if (size() < 3)
    {
        Error("Winding::Check : %zu points", size());
    }

    area = getArea();
    if (area < 1)
    {
        Error("Winding::Check : %f area", area);
    }

    getPlane(facenormal, facedist);

    for (i = 0; i < size(); i++)
    {
        const vec3_array& p1 = m_Points[i];

        for (j = 0; j < 3; j++)
        {
            if (p1[j] > bogus_range || p1[j] < -bogus_range)
            {
                Error("Winding::Check : bogus_range: %f", p1[j]);
            }
        }

        j = i + 1 == size() ? 0 : i + 1;

        // check the point is on the face plane
        d = DotProduct(p1, facenormal) - facedist;
        if (d < -epsilon || d > epsilon)
        {
            Error("Winding::Check : point off plane");
        }

        // check the edge isn't degenerate
        const vec3_array& p2 = m_Points[j];
        VectorSubtract(p2, p1, dir);

        if (VectorLength(dir) < epsilon)
        {
            Error("Winding::Check : degenerate edge");
        }

        CrossProduct(facenormal, dir, edgenormal);
        VectorNormalize(edgenormal);
        edgedist = DotProduct(p1, edgenormal);
        edgedist += epsilon;

        // all other points must be on front side
        for (j = 0; j < size(); j++)
        {
            if (j == i)
            {
                continue;
            }
            d = DotProduct(m_Points[j], edgenormal);
            if (d > edgedist)
            {
                Error("Winding::Check : non-convex");
            }
        }
    }
}

bool          Winding::Valid() const
{
    // TODO: Check to ensure there are 3 non-colinear points
    return size() >= 3;
}

//
// Construction
//

Winding::Winding()
{}

Winding::Winding(vec3_array *points, std::size_t numpoints)
{
	hlassert(numpoints >= 3);
    std::size_t capacity = (numpoints + 3) & ~3;   // groups of 4
    m_Points.reserve(capacity);
    m_Points.assign_range(std::span(points, numpoints));
}

Winding&      Winding::operator=(const Winding& other)
{
    m_Points = other.m_Points;
    return *this;
}
Winding&      Winding::operator=(Winding&& other)
{
    m_Points = std::move(other.m_Points);
    return *this;
}



Winding::Winding(std::uint_least32_t numpoints)
{
    hlassert(numpoints >= 3);
    std::size_t capacity = (numpoints + 3) & ~3;   // groups of 4

    m_Points.reserve(capacity);
    m_Points.resize(numpoints);
}

Winding::Winding(const Winding& other)
{
    m_Points = other.m_Points;
}

Winding::Winding(Winding&& other):
    m_Points(std::move(other.m_Points))
{ }

Winding::~Winding()
{ }


void Winding::initFromPlane(const vec3_array& normal, const vec_t dist)
{
    int             i;
    vec_t           max, v;
    vec3_array          org, vright, vup;

    // find the major axis               

    max = -bogus_range;
    int x = -1;
    for (i = 0; i < 3; i++)          
    {
        v = fabs(normal[i]);        
        if (v > max)
        {
            max = v;
            x = i;
        }
    }                
    if (x == -1)
    {
        Error("Winding::initFromPlane no major axis found\n");
    }

    VectorCopy(vec3_origin, vup);
    switch (x) 
    {
    case 0:
    case 1:          
        vup[2] = 1;
        break;
    case 2:
        vup[0] = 1;      
        break;
    }

    v = DotProduct(vup, normal);
    VectorMA(vup, -v, normal, vup);
    VectorNormalize(vup);

    VectorScale(normal, dist, org);

    CrossProduct(vup, normal, vright);

    VectorScale(vup, bogus_range, vup);
    VectorScale(vright, bogus_range, vright);

    // project a really big     axis aligned box onto the plane
    m_Points.resize(4);

    VectorSubtract(org, vright, m_Points[0]);
    VectorAdd(m_Points[0], vup, m_Points[0]);

    VectorAdd(org, vright, m_Points[1]);
    VectorAdd(m_Points[1], vup, m_Points[1]);

    VectorAdd(org, vright, m_Points[2]);
    VectorSubtract(m_Points[2], vup, m_Points[2]);

    VectorSubtract(org, vright, m_Points[3]);
    VectorSubtract(m_Points[3], vup, m_Points[3]);
}

Winding::Winding(const vec3_array& normal, const vec_t dist)
{
    initFromPlane(normal, dist);
}

Winding::Winding(const dface_t& face
				 , vec_t epsilon
				 )
{
    int             se;
    dvertex_t*      dv;
    int             v;

    m_Points.resize(face.numedges);

    unsigned i;
    for (i = 0; i < face.numedges; i++)
    {
        se = g_dsurfedges[face.firstedge + i];
        if (se < 0)
        {
            v = g_dedges[-se].v[1];
        }
        else
        {
            v = g_dedges[se].v[0];
        }

        dv = &g_dvertexes[v];
        VectorCopy(dv->point, m_Points[i]);
    }

    RemoveColinearPoints(
		epsilon
		);
}

Winding::Winding(const dplane_t& plane)
{
    vec3_array normal;
    vec_t dist;

    VectorCopy(plane.normal, normal);
    dist = plane.dist;
    initFromPlane(normal, dist);
}

//
// Specialized Functions
//

// Remove the colinear point of any three points that forms a triangle which is thinner than epsilon
void			Winding::RemoveColinearPoints(
											  vec_t epsilon
											  )
{
	unsigned int	i;
	vec3_array v1, v2;
	vec_t			*p1, *p2, *p3;
	for (i = 0; i < size(); i++)
	{
		p1 = m_Points[(i+size()-1)%size()].data();
		p2 = m_Points[i].data();
		p3 = m_Points[(i+1)%size()].data();
		VectorSubtract (p2, p1, v1);
		VectorSubtract (p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct (v1, v2) * DotProduct (v1, v2) >= DotProduct (v1, v1) * DotProduct (v2, v2) 
			- epsilon * epsilon * (DotProduct (v1, v1) + DotProduct (v2, v2) + epsilon * epsilon))
			// v2 == k * v1 + v3 && abs (v3) < epsilon || v1 == k * v2 + v3 && abs (v3) < epsilon
		{
			for (; i < size() - 1; i++)
			{
				VectorCopy (m_Points[i+1], m_Points[i]);
			}
            m_Points.pop_back();
			i = -1;
			continue;
		}
	}
}


void Winding::Clip(const dplane_t& plane, std::optional<Winding>& front, std::optional<Winding>& back
							  , vec_t epsilon
							  ) const
{
    vec3_array normal;
    vec_t dist;
    VectorCopy(plane.normal, normal);
    dist = plane.dist;
    Clip(normal, dist, front, back
		, epsilon
		);
}


void Winding::Clip(const vec3_array& normal, const vec_t dist, std::optional<Winding>& front, std::optional<Winding>& back
							  , vec_t epsilon
							  ) const
{
    vec_t           dists[MAX_POINTS_ON_WINDING + 4];
    int             sides[MAX_POINTS_ON_WINDING + 4];
    std::array<std::size_t, 3> counts{};
    vec_t           dot;
    unsigned int    i;
    unsigned int    maxpts;

    // determine sides for each point
    for (i = 0; i < size(); i++)
    {
        dot = DotProduct(m_Points[i], normal);
        dot -= dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    if (!counts[SIDE_FRONT])
    {
        front = std::nullopt;
        back = *this;
        return;
    }
    if (!counts[SIDE_BACK])
    {
        front = *this;
        back = std::nullopt;
        return;
    }

    Winding f{};
    Winding b{};
    f.m_Points.reserve(size() + 4); // Optimization only - can safely be removed
    b.m_Points.reserve(size() + 4); // Optimization only - can safely be removed

    for (i = 0; i < size(); i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            f.m_Points.emplace_back(p1);
            b.m_Points.emplace_back(p1);
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            f.m_Points.emplace_back(p1);
        }
        else if (sides[i] == SIDE_BACK)
        {
            b.m_Points.emplace_back(p1);
        }

        if ((sides[i + 1] == SIDE_ON) | (sides[i + 1] == sides[i]))  // | instead of || for branch optimization
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= size())
        {
            tmp = 0;
        }
        const vec3_array& p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);

        for (std::size_t j = 0; j < 3; j++)
        {                                                  // avoid round off error when possible
            if (normal[j] == 1)
                mid[j] = dist;
            else if (normal[j] == -1)
                mid[j] = -dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        f.m_Points.emplace_back(mid);
        b.m_Points.emplace_back(mid);
    }

    if ((f.size() > MAX_POINTS_ON_WINDING) | (b.size() > MAX_POINTS_ON_WINDING)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : MAX_POINTS_ON_WINDING");
    }
    f.RemoveColinearPoints(
		epsilon
		);
    b.RemoveColinearPoints(
		epsilon
		);


	if (f.m_Points.empty())
	{
        front = std::nullopt;
	} else {
        front = std::move(f);
    }
	if (b.m_Points.empty())
	{
        back = std::nullopt;
	} else {
        back = std::move(b);
    }
}

bool Winding::Chop(const vec3_array& normal, const vec_t dist
							, vec_t epsilon
							)
{
    std::optional<Winding> f;
    std::optional<Winding> b;

    Clip(normal, dist, f, b, epsilon);

    if (f)
    {
    	using std::swap;
        swap(*this, f.value());
        return true;
    }

    m_Points.clear();
    m_Points.shrink_to_fit();
    return false;
}

int Winding::WindingOnPlaneSide(const vec3_array& normal, const vec_t dist
											, vec_t epsilon
											)
{
    bool            front, back;
    unsigned int    i;
    vec_t           d;

    front = false;
    back = false;
    for (i = 0; i < size(); i++)
    {
        d = DotProduct(m_Points[i], normal) - dist;
        if (d < -epsilon)
        {
            if (front)
            {
                return SIDE_CROSS;
            }
            back = true;
            continue;
        }
        if (d > epsilon)
        {
            if (back)
            {
                return SIDE_CROSS;
            }
            front = true;
            continue;
        }
    }

    if (back)
    {
        return SIDE_BACK;
    }
    if (front)
    {
        return SIDE_FRONT;
    }
    return SIDE_ON;
}


bool Winding::Clip(const dplane_t& split, bool keepon
				   , vec_t epsilon
				   )
{
    vec_t           dists[MAX_POINTS_ON_WINDING];
    int             sides[MAX_POINTS_ON_WINDING];
    int             counts[3];
    vec_t           dot;
    int             i, j;

    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    // do this exactly, with no epsilon so tiny portals still work
    for (i = 0; i < size(); i++)
    {
        dot = DotProduct(m_Points[i], split.normal);
        dot -= split.dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    if (keepon && !counts[0] && !counts[1])
    {
        return true;
    }

    if (!counts[0])
    {
        m_Points.clear();
        m_Points.shrink_to_fit();
        return false;
    }

    if (!counts[1])
    {
        return true;
    }

    std::vector<vec3_array> newPoints{};
    newPoints.reserve(size() + 4);

    for (i = 0; i < size(); i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            newPoints.emplace_back(p1);
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            newPoints.emplace_back(p1);
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= size())
        {
            tmp = 0;
        }
        const vec3_array& p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++)
        {                                                  // avoid round off error when possible
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }
        newPoints.emplace_back(mid);
    }

	using std::swap;
    swap(m_Points, newPoints);

    RemoveColinearPoints(
		epsilon
		);

	if (m_Points.empty())
	{
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

void Winding::Divide(const dplane_t& split, Winding** front, Winding** back
					 , vec_t epsilon
					 )
{
    vec_t           dists[MAX_POINTS_ON_WINDING];
    int             sides[MAX_POINTS_ON_WINDING];
    int             counts[3];
    vec_t           dot;
    int             i, j;

    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    for (i = 0; i < size(); i++)
    {
        dot = DotProduct(m_Points[i], split.normal);
        dot -= split.dist;
        dists[i] = dot;
        if (dot > epsilon)
        {
            sides[i] = SIDE_FRONT;
        }
        else if (dot < -epsilon)
        {
            sides[i] = SIDE_BACK;
        }
        else
        {
            sides[i] = SIDE_ON;
        }
        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    *front = *back = nullptr;

	if (!counts[0] && !counts[1])
	{
		vec_t sum = 0.0;
		for (i = 0; i < size(); i++)
		{
			dot = DotProduct(m_Points[i], split.normal);
			dot -= split.dist;
			sum += dot;
		}
		if (sum > NORMAL_EPSILON)
		{
			*front = this;
		}
		else
		{
			*back = this;
		}
		return;
	}
    if (!counts[0])
    {
        *back = this;   // Makes this function non-const
        return;
    }
    if (!counts[1])
    {
        *front = this;  // Makes this function non-const
        return;
    }

    Winding* f = new Winding{};
    Winding* b = new Winding{};
    f->m_Points.reserve(size() + 4);
    b->m_Points.reserve(size() + 4);

    *front = f;
    *back = b;

    for (i = 0; i < size(); i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            f->m_Points.emplace_back(p1);
            b->m_Points.emplace_back(p1);
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            f->m_Points.emplace_back(p1);
        }
        else if (sides[i] == SIDE_BACK)
        {
            b->m_Points.emplace_back(p1);
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= size())
        {
            tmp = 0;
        }
        const vec3_array& p2 = m_Points[tmp];
        dot = dists[i] / (dists[i] - dists[i + 1]);
        for (j = 0; j < 3; j++)
        {                                                  // avoid round off error when possible
            if (split.normal[j] == 1)
                mid[j] = split.dist;
            else if (split.normal[j] == -1)
                mid[j] = -split.dist;
            else
                mid[j] = p1[j] + dot * (p2[j] - p1[j]);
        }

        f->m_Points.emplace_back(mid);
        b->m_Points.emplace_back(mid);
    }

    f->RemoveColinearPoints(
		epsilon
		);
    b->RemoveColinearPoints(
		epsilon
		);
	if (f->m_Points.empty())
	{
		delete f;
		delete b;
		*front = nullptr;
		*back = this;
	}
	else if (b->m_Points.empty())
	{
		delete f;
		delete b;
		*back = nullptr;
		*front = this;
	}
}



// Unused??
void Winding::insertPoint(const vec3_array& newpoint, std::size_t offset)
{
    grow_capacity();
    m_Points.insert(m_Points.begin() + offset, newpoint);
}

// Unused??
void Winding::pushPoint(const vec3_array& newpoint)
{
    grow_capacity();
    m_Points.emplace_back(newpoint);
}

std::size_t Winding::size() const
{
    return m_Points.size();
}

void Winding::grow_capacity()
{
    std::size_t newsize = size() + 1;
    newsize = (newsize + 3) & ~3; // Groups of 4
    m_Points.resize(newsize);
}
