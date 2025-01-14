// AJM: added this file in

#include "csg.h"
#include "worldspawn_wad_value_parser.h"

std::vector<wadpath_t*> g_pWadPaths;


// =====================================================================================
//  PushWadPath
//      adds a wadpath into the wadpaths list, without duplicates
// =====================================================================================
void PushWadPath(std::u8string_view path, bool inuse)
{
    std::unique_ptr<wadpath_t> currentWad = std::make_unique<wadpath_t>();
	
	currentWad->path = path;
	currentWad->usedbymap = inuse;
	currentWad->usedtextures = 0;  //Updated later in autowad procedures
	currentWad->totaltextures = 0; //Updated later to reflect total

	g_pWadPaths.push_back(currentWad.release());
}


// =====================================================================================
//  FreeWadPaths
// =====================================================================================
void        FreeWadPaths()
{
    int         i;
    wadpath_t*  current;

    for (i = 0; i < g_pWadPaths.size(); i++)
    {
        current = g_pWadPaths[i];
        delete current;
    }
}

// =====================================================================================
//  GetUsedWads
//      parse the "wad" keyvalue into wadpath_t structs
// =====================================================================================
void        GetUsedWads() {
    std::u8string_view wadValue = value_for_key(&g_entities[0], u8"wad");

	for(std::u8string_view wadFilename : worldspawn_wad_value_parser(wadValue)) {
		PushWadPath(wadFilename, true);
	}
}