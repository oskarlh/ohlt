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

    for (x=0; x<m_NumPoints; x++)
    {
        Log("(%5.2f, %5.2f, %5.2f)\n", m_Points[x][0], m_Points[x][1], m_Points[x][2]);
    }
}

void Winding::getPlane(dplane_t& plane) const
{
    vec3_array          v1, v2;
    vec3_array          plane_normal;

    //hlassert(m_NumPoints >= 3);

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[2], m_Points[1], v1);
        VectorSubtract(m_Points[0], m_Points[1], v2);

        CrossProduct(v2, v1, plane_normal);
        VectorNormalize(plane_normal);
        VectorCopy(plane_normal, plane.normal);               // change from vec_t
        plane.dist = DotProduct(m_Points[0], plane.normal);
    }
    else
    {
        VectorClear(plane.normal);
        plane.dist = 0.0;
    }
}

void            Winding::getPlane(vec3_array& normal, vec_t& dist) const
{
    vec3_array          v1, v2;

    //hlassert(m_NumPoints >= 3);

    if (m_NumPoints >= 3)
    {
        VectorSubtract(m_Points[1], m_Points[0], v1);
        VectorSubtract(m_Points[2], m_Points[0], v2);
        CrossProduct(v2, v1, normal);
        VectorNormalize(normal);
        dist = DotProduct(m_Points[0], normal);
    }
    else
    {
        VectorClear(normal);
        dist = 0.0;
    }
}

vec_t           Winding::getArea() const
{
    unsigned int    i;
    vec3_array          d1, d2, cross;
    vec_t           total;

    //hlassert(m_NumPoints >= 3);

    total = 0.0;
    if (m_NumPoints >= 3)
    {
        for (i = 2; i < m_NumPoints; i++)
        {
            VectorSubtract(m_Points[i - 1], m_Points[0], d1);
            VectorSubtract(m_Points[i], m_Points[0], d2);
            CrossProduct(d1, d2, cross);
            total += 0.5 * VectorLength(cross);
        }
    }
    return total;
}

void            Winding::getBounds(vec3_array& mins, vec3_array& maxs) const
{
    bounding_box     bounds;
    unsigned    x;

    for (x=0; x<m_NumPoints; x++)
    {
        add_to_bounding_box(bounds, m_Points[x]);
    }
    VectorCopy(bounds.maxs, maxs);
    VectorCopy(bounds.mins, mins);
}

void            Winding::getBounds(bounding_box& bounds) const
{
    reset_bounding_box(bounds);
    unsigned    x;

    for (x=0; x<m_NumPoints; x++)
    {
        add_to_bounding_box(bounds, m_Points[x]);
    }
}

vec3_array Winding::getCenter() const
{
    vec3_array center;
    unsigned int    i;
    vec_t           scale;

    if (m_NumPoints > 0)
    {
        VectorCopy(vec3_origin, center);
        for (i = 0; i < m_NumPoints; i++)
        {
            VectorAdd(m_Points[i], center, center);
        }

        scale = 1.0 / m_NumPoints;
        VectorScale(center, scale, center);
    }
    else
    {
        VectorClear(center);
    }
    return center;
}

Winding*        Winding::Copy() const
{
    Winding* newWinding = new Winding(*this);
    return newWinding;
}

void            Winding::Check(
							   vec_t epsilon
							   ) const
{
    unsigned int    i, j;
    vec_t           d, edgedist;
    vec3_array          dir, edgenormal, facenormal;
    vec_t           area;
    vec_t           facedist;

    if (m_NumPoints < 3)
    {
        Error("Winding::Check : %i points", m_NumPoints);
    }

    area = getArea();
    if (area < 1)
    {
        Error("Winding::Check : %f area", area);
    }

    getPlane(facenormal, facedist);

    for (i = 0; i < m_NumPoints; i++)
    {
        const vec3_array& p1 = m_Points[i];

        for (j = 0; j < 3; j++)
        {
            if (p1[j] > bogus_range || p1[j] < -bogus_range)
            {
                Error("Winding::Check : bogus_range: %f", p1[j]);
            }
        }

        j = i + 1 == m_NumPoints ? 0 : i + 1;

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
        for (j = 0; j < m_NumPoints; j++)
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
    if ((m_NumPoints < 3) || (m_Points.empty()))
    {
        return false;
    }
    return true;
}

//
// Construction
//

Winding::Winding()
{}

Winding::Winding(vec3_array *points, std::uint_least32_t numpoints)
{
	hlassert(numpoints >= 3);
	m_NumPoints = numpoints;
    std::size_t capacity = (m_NumPoints + 3) & ~3;   // groups of 4

    m_Points.reserve(capacity);
    m_Points.assign_range(std::span(points, m_NumPoints));
	m_Points.resize(capacity, {});
}

Winding&      Winding::operator=(const Winding& other)
{
    m_NumPoints = other.m_NumPoints;
    m_Points = other.m_Points;
    return *this;
}
Winding&      Winding::operator=(Winding&& other)
{
    m_NumPoints = other.m_NumPoints;
    m_Points = std::move(other.m_Points);
    return *this;
}



Winding::Winding(std::uint_least32_t numpoints)
{
    hlassert(numpoints >= 3);
    m_NumPoints = numpoints;
    std::size_t capacity = (m_NumPoints + 3) & ~3;   // groups of 4

    m_Points.resize(capacity);
}

Winding::Winding(const Winding& other)
{
    m_NumPoints = other.m_NumPoints;
    m_Points = other.m_Points;
}

Winding::Winding(Winding&& other): 
    m_NumPoints(other.m_NumPoints),
    m_Points(std::move(other.m_Points))
{
}

Winding::~Winding()
{
}


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
    m_NumPoints = 4;
    m_Points.resize(m_NumPoints);

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

    m_NumPoints = face.numedges;
    m_Points.resize(m_NumPoints);

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
	for (i = 0; i < m_NumPoints; i++)
	{
		p1 = m_Points[(i+m_NumPoints-1)%m_NumPoints].data();
		p2 = m_Points[i].data();
		p3 = m_Points[(i+1)%m_NumPoints].data();
		VectorSubtract (p2, p1, v1);
		VectorSubtract (p3, p2, v2);
		// v1 or v2 might be close to 0
		if (DotProduct (v1, v2) * DotProduct (v1, v2) >= DotProduct (v1, v1) * DotProduct (v2, v2) 
			- epsilon * epsilon * (DotProduct (v1, v1) + DotProduct (v2, v2) + epsilon * epsilon))
			// v2 == k * v1 + v3 && abs (v3) < epsilon || v1 == k * v2 + v3 && abs (v3) < epsilon
		{
			m_NumPoints--;
			for (; i < m_NumPoints; i++)
			{
				VectorCopy (m_Points[i+1], m_Points[i]);
			}
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
    for (i = 0; i < m_NumPoints; i++)
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

    maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because
    // of fp grouping errors

    Winding f{maxpts};
    Winding b{maxpts};

    f.m_NumPoints = 0;
    b.m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f.m_Points[f.m_NumPoints]);
            VectorCopy(p1, b.m_Points[b.m_NumPoints]);
            f.m_NumPoints++;
            b.m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f.m_Points[f.m_NumPoints]);
            f.m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b.m_Points[b.m_NumPoints]);
            b.m_NumPoints++;
        }

        if ((sides[i + 1] == SIDE_ON) | (sides[i + 1] == sides[i]))  // | instead of || for branch optimization
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
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

        VectorCopy(mid, f.m_Points[f.m_NumPoints]);
        VectorCopy(mid, b.m_Points[b.m_NumPoints]);
        f.m_NumPoints++;
        b.m_NumPoints++;
    }

    if ((f.m_NumPoints > maxpts) | (b.m_NumPoints > maxpts)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : points exceeded estimate");
    }
    if ((f.m_NumPoints > MAX_POINTS_ON_WINDING) | (b.m_NumPoints > MAX_POINTS_ON_WINDING)) // | instead of || for branch optimization
    {
        Error("Winding::Clip : MAX_POINTS_ON_WINDING");
    }
    f.RemoveColinearPoints(
		epsilon
		);
    b.RemoveColinearPoints(
		epsilon
		);


	if (f.m_NumPoints == 0)
	{
        front = std::nullopt;
	} else {
        front = std::move(f);
    }
	if (b.m_NumPoints == 0)
	{
        back = std::nullopt;
	} else {
        back = std::move(b);
    }
}

bool          Winding::Chop(const vec3_array& normal, const vec_t dist
							, vec_t epsilon
							)
{
    std::optional<Winding> f;
    std::optional<Winding> b;

    Clip(normal, dist, f, b
		, epsilon
		);

    if (f)
    {
        m_NumPoints = f->m_NumPoints;
    	using std::swap;
        swap(m_Points, f->m_Points);
        return true;
    }
    else
    {
        m_NumPoints = 0;
        m_Points.clear();
        m_Points.shrink_to_fit();
        return false;
    }
}

int             Winding::WindingOnPlaneSide(const vec3_array& normal, const vec_t dist
											, vec_t epsilon
											)
{
    bool            front, back;
    unsigned int    i;
    vec_t           d;

    front = false;
    back = false;
    for (i = 0; i < m_NumPoints; i++)
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
    for (i = 0; i < m_NumPoints; i++)
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
        m_NumPoints = 0;
        return false;
    }

    if (!counts[1])
    {
        return true;
    }

    unsigned maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because of fp grouping errors
    unsigned newNumPoints = 0;
    std::vector<vec3_array> newPoints(maxpts);

    for (i = 0; i < m_NumPoints; i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, newPoints[newNumPoints]);
            newNumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, newPoints[newNumPoints]);
            newNumPoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
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

        VectorCopy(mid, newPoints[newNumPoints]);
        newNumPoints++;
    }

    if (newNumPoints > maxpts)
    {
        Error("Winding::Clip : points exceeded estimate");
    }

	using std::swap;
    swap(m_Points, newPoints);
    m_NumPoints = newNumPoints;

    RemoveColinearPoints(
		epsilon
		);
	if (m_NumPoints == 0)
	{

        m_Points.clear();
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
    int             maxpts;

    counts[0] = counts[1] = counts[2] = 0;

    // determine sides for each point
    for (i = 0; i < m_NumPoints; i++)
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
		for (i = 0; i < m_NumPoints; i++)
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

    maxpts = m_NumPoints + 4;                            // can't use counts[0]+2 because
    // of fp grouping errors

    Winding* f = new Winding(maxpts);
    Winding* b = new Winding(maxpts);

    *front = f;
    *back = b;

    f->m_NumPoints = 0;
    b->m_NumPoints = 0;

    for (i = 0; i < m_NumPoints; i++)
    {
        const vec3_array& p1 = m_Points[i];

        if (sides[i] == SIDE_ON)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            f->m_NumPoints++;
            b->m_NumPoints++;
            continue;
        }
        else if (sides[i] == SIDE_FRONT)
        {
            VectorCopy(p1, f->m_Points[f->m_NumPoints]);
            f->m_NumPoints++;
        }
        else if (sides[i] == SIDE_BACK)
        {
            VectorCopy(p1, b->m_Points[b->m_NumPoints]);
            b->m_NumPoints++;
        }

        if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
        {
            continue;
        }

        // generate a split point
        vec3_array mid;
        unsigned int tmp = i + 1;
        if (tmp >= m_NumPoints)
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

        VectorCopy(mid, f->m_Points[f->m_NumPoints]);
        VectorCopy(mid, b->m_Points[b->m_NumPoints]);
        f->m_NumPoints++;
        b->m_NumPoints++;
    }

    if (f->m_NumPoints > maxpts || b->m_NumPoints > maxpts)
    {
        Error("Winding::Divide : points exceeded estimate");
    }

    f->RemoveColinearPoints(
		epsilon
		);
    b->RemoveColinearPoints(
		epsilon
		);
	if (f->m_NumPoints == 0)
	{
		delete f;
		delete b;
		*front = nullptr;
		*back = this;
	}
	else if (b->m_NumPoints == 0)
	{
		delete f;
		delete b;
		*back = nullptr;
		*front = this;
	}
}


void Winding::addPoint(const vec3_array& newpoint)
{
    grow_capacity();
    VectorCopy(newpoint, m_Points[m_NumPoints]);
    m_NumPoints++;
}


void Winding::insertPoint(const vec3_array& newpoint, const unsigned int offset)
{
    if (offset >= m_NumPoints)
    {
        addPoint(newpoint);
    }
    else
    {
        grow_capacity();

        unsigned x;
        for (x = m_NumPoints; x>offset; x--)
        {
            VectorCopy(m_Points[x-1], m_Points[x]);
        }
        VectorCopy(newpoint, m_Points[x]);

        m_NumPoints++;
    }
}


void Winding::grow_capacity()
{
    std::uint_least32_t newsize = m_NumPoints + 1;
    newsize = (newsize + 3) & ~3;   // groups of 4
    m_Points.resize(newsize);
}
