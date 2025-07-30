#pragma once
#include <string>
#include <vector>

struct wadpath_t final {
	std::u8string path;
	int totaltextures; // Total number of textures in this WAD
	int usedtextures;  // Number of textures in this WAD the map actually
					   // uses
	bool usedbymap; // Does the map require this WAD to be included in the
					// .bsp?
	// !!! the above two are VERY DIFFERENT. ie (usedtextures == 0) !=
	// (usedbymap == false)
};

extern std::vector<wadpath_t*> g_pWadPaths;

extern void PushWadPath(std::u8string_view path, bool inuse);
extern void FreeWadPaths();
extern void GetUsedWads();
