// AJM: added this file in

#include "csg.h"

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
void        GetUsedWads()
{
    const char* pszWadPaths;
    int i, j;
    pszWadPaths = (const char*) ValueForKey(&g_entities[0], u8"wad");

	for (i = 0; ; ) //Loop through wadpaths
	{
		for (j = i; pszWadPaths[j] != '\0'; j++) //Find end of wadpath (semicolon)
		{
			if (pszWadPaths[j] == ';')
			{
				break;
			}
		}
		if (j - i > 0) //If wadpath is not empty
		{
			int length = std::min(j - i, _MAX_PATH - 1); //Get length of wadpath
			std::u8string_view wp{&((const char8_t*) pszWadPaths)[i], &((const char8_t*) pszWadPaths)[i + length]};
			PushWadPath (wp, true); //Add wadpath to list
		}
		if (pszWadPaths[j] == '\0') //Break if end of wadpaths
		{
			break;
		}
		i = j + 1;
	}
}