#include "vis.h"


void Zones::set(std::uint_least32_t zone, const BoundingBox& bounds)
{
    if (zone < m_ZoneCount)
    {
        m_ZoneBounds[zone] = bounds;
    }
}

std::uint_least32_t Zones::getZoneFromBounds(const BoundingBox& bounds)
{
    std::uint_least32_t x;
    for (x=0; x<m_ZoneCount; x++)
    {
        if (m_ZoneBounds[x].testSuperset(bounds))
        {
            return x;
        }
    }
    return 0;
}

std::uint_least32_t Zones::getZoneFromWinding(const Winding& winding)
{
    std::uint_least32_t          x;
    BoundingBox     bounds;

    for (x=0; x<winding.m_NumPoints; x++)
    {
        bounds.add(winding.m_Points[x]);
    }

    return getZoneFromBounds(bounds);
}

// BORROWED FROM HLRAD
// TODO: Consolite into common sometime
static Winding*      WindingFromFace(const dface_t* f)
{
    int             i;
    int             se;
    dvertex_t*      dv;
    int             v;
    Winding*        w = new Winding(f->numedges);

    for (i = 0; i < f->numedges; i++)
    {
        se = g_dsurfedges[f->firstedge + i];
        if (se < 0)
        {
            v = g_dedges[-se].v[1];
        }
        else
        {
            v = g_dedges[se].v[0];
        }

        dv = &g_dvertexes[v];
        VectorCopy(dv->point, w->m_Points[i]);
    }

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
        dmodel_t*       mod = g_dmodels + x;
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
                BoundingBox     bounds;
                dface_t*        f = g_dfaces + mod->firstface;
            
                for (j = 0; j < mod->numfaces; j++, f++)
                {
                    Winding*        w = WindingFromFace(f);
                    std::uint_least32_t          k;

                    for (k = 0; k < w->m_NumPoints; k++)
                    {
                        bounds.add(w->m_Points[k]);
                    }
                    delete w;
                }

                zones->set(func_vis_id, bounds);

                Log("Adding zone %u : mins(%4.3f %4.3f %4.3f) maxs(%4.3f %4.3f %4.3f)\n", func_vis_id, 
                    bounds.m_Mins[0],bounds.m_Mins[1],bounds.m_Mins[2],
                    bounds.m_Maxs[0],bounds.m_Maxs[1],bounds.m_Maxs[2]);
            }
        }
    }

    return zones;
}
