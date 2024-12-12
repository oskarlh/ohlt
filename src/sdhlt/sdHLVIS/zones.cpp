#include "vis.h"


void Zones::set(std::uint_least32_t zone, const bounding_box& bounds)
{
    if (zone < m_ZoneCount)
    {
        set_bounding_box(m_ZoneBounds[zone], bounds);
    }
}

std::uint_least32_t Zones::getZoneFromBounds(const bounding_box& bounds)
{
    std::uint_least32_t x;
    for (x=0; x<m_ZoneCount; x++)
    {
        if (test_superset(m_ZoneBounds[x], bounds))
        {
            return x;
        }
    }
    return 0;
}

std::uint_least32_t Zones::getZoneFromWinding(const Winding& winding)
{
    std::uint_least32_t          x;
    bounding_box     bounds;

    for (x=0; x<winding.size(); x++)
    {
        add_to_bounding_box(bounds, winding.m_Points[x]);
    }

    return getZoneFromBounds(bounds);
}

// BORROWED FROM HLRAD
// TODO: Consolite into common sometime
static Winding WindingFromFace(const dface_t* f)
{
    int             i;
    dvertex_t*      dv;
    int             v;

    std::vector<vec3_array> windingPoints;
    for (i = 0; i < f->numedges; i++)
    {
        std::int32_t se = g_dsurfedges[f->firstedge + i];
        std::int32_t index = std::abs(se);
        if (se < 0)
        {
            v = g_dedges[index].v[1];
        }
        else
        {
            v = g_dedges[index].v[0];
        }

        dv = &g_dvertexes[v];
        windingPoints.emplace_back(dv->point);
    }

    Winding w;
    using std::swap;
    swap(w.m_Points, windingPoints);
    return w;
}

Zones* MakeZones(void)
{
    std::uint_least32_t x;
    std::uint_least32_t func_vis_count = 0;

    ParseEntities();

    // Note: we arent looping through entities because we only care if it has a winding/bounding box

    // First count the number of func_vis's
    for (x=0; x<g_nummodels; x++)
    {
        entity_t*       ent = EntityForModel(x);

        if (!strcasecmp((const char*) ValueForKey(ent, u8"classname"), "func_vis"))
        {
            std::uint_least32_t value = atoi((const char*) ValueForKey(ent, u8"node"));
            if (value)
            {
                func_vis_count++;
            }
            else
            {
                Error("func_vis with no \"node\" id\n");
            }
        }
    }

    if (!func_vis_count)
    {
        return nullptr;
    }

    Zones* zones = new Zones(func_vis_count);

    for (x=0; x<g_nummodels; x++)
    {
        dmodel_t*       mod = &g_dmodels[x];
        entity_t*       ent = EntityForModel(x);

        if (!strcasecmp((const char*) ValueForKey(ent, u8"classname"), "func_vis"))
        {
            std::uint_least32_t func_vis_id = atoi((const char*) ValueForKey(ent, u8"node"));

            {
                for (const epair_t* keyvalue = ent->epairs; keyvalue; keyvalue = keyvalue->next)
                {
                    std::uint_least32_t other_id = atoi((const char*) keyvalue->key.c_str());
                    if (other_id)
                    {
                        zones->flag(func_vis_id, other_id);
                    }
                }
            }
    
            {
                std::uint_least32_t          j;
                bounding_box     bounds;
                dface_t*        f = &g_dfaces[mod->firstface];
            
                for (j = 0; j < mod->numfaces; j++, f++)
                {
                    for (const vec3_array& windingPoint : WindingFromFace(f).m_Points)
                    {
                        add_to_bounding_box(bounds, windingPoint);
                    }
                }

                zones->set(func_vis_id, bounds);

                Log("Adding zone %u : mins(%4.3f %4.3f %4.3f) maxs(%4.3f %4.3f %4.3f)\n", func_vis_id, 
                    bounds.mins[0],bounds.mins[1],bounds.mins[2],
                    bounds.maxs[0],bounds.maxs[1],bounds.maxs[2]);
            }
        }
    }

    return zones;
}
