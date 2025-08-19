#include "cmdlib.h"
#include "filelib.h"
#include "hlcsg.h"
#include "log.h"
#include "threads.h"
#include "time_counter.h"
#include "util.h"
#include "wad_structs.h"

#include <tuple>
#include <vector>

using namespace std::literals;

#define MAX_TEXFILES 128

//  FindMiptex
//  TEX_InitFromWad
//  FindTexture
//  LoadLump
//  AddAnimatingTextures

struct lumpinfo_with_wadfileindex final {
	wad_lumpinfo lump_info;
	int iTexFile; // index of the wad this texture is located in
};

std::deque<std::u8string> g_WadInclude;

static int nummiptex = 0;
static lumpinfo_with_wadfileindex miptex[MAX_MAP_TEXTURES];
static int nTexLumps = 0;
static lumpinfo_with_wadfileindex* lumpinfo = nullptr;
static int nTexFiles = 0;
static FILE* texfiles[MAX_TEXFILES];
static wadpath_t*
	texwadpathes[MAX_TEXFILES]; // maps index of the wad to its path

// The old buggy code in effect limit the number of brush sides to
// MAX_MAP_BRUSHES

static wad_texture_name texmap[INITIAL_MAX_MAP_TEXINFO];

static int numtexmap = 0;

static int texmap_store(wad_texture_name texname)
// This function should never be called unless a new entry in g_texinfo is
// being allocated. Also, should be called between ThreadLock() and
// ThreadUnlock()!!
{
	hlassume(
		numtexmap < INITIAL_MAX_MAP_TEXINFO,
		assume_msg::INITIAL_MAX_MAP_TEXINFO
	); // This error should never appear.

	int i = numtexmap;
	texmap[numtexmap] = texname;
	numtexmap++;
	return i;
}

static wad_texture_name texmap_retrieve(int index) {
	hlassume(0 <= index && index < numtexmap, assume_msg::first);
	return texmap[index];
}

static void texmap_clear() {
	ThreadLock();
	numtexmap = 0;
	ThreadUnlock();
}

// =====================================================================================
//  lump_sorters
// =====================================================================================

static int
lump_sorter_by_wad_and_name(void const * lump1, void const * lump2) {
	lumpinfo_with_wadfileindex* plump1 = (lumpinfo_with_wadfileindex*)
		lump1;
	lumpinfo_with_wadfileindex* plump2 = (lumpinfo_with_wadfileindex*)
		lump2;

	std::strong_ordering comparisonResult{
		std::tie(plump1->iTexFile, plump1->lump_info.name)
		<=> std::tie(plump2->iTexFile, plump2->lump_info.name)
	};

	return ordering_as_int(comparisonResult);
}

static int lump_sorter_by_name(void const * lump1, void const * lump2) {
	lumpinfo_with_wadfileindex* plump1 = (lumpinfo_with_wadfileindex*)
		lump1;
	lumpinfo_with_wadfileindex* plump2 = (lumpinfo_with_wadfileindex*)
		lump2;

	std::strong_ordering comparisonResult{ plump1->lump_info.name
		                                   <=> plump2->lump_info.name };

	return ordering_as_int(comparisonResult);
}

// =====================================================================================
//  FindMiptex
//      Find and allocate a texture into the lump data
// =====================================================================================
static int FindMiptex(wad_texture_name name) {
	ThreadLock();
	for (std::size_t i = 0; i < nummiptex; i++) {
		if (name == miptex[i].lump_info.name) {
			ThreadUnlock();
			return i;
		}
	}

	hlassume(nummiptex < MAX_MAP_TEXTURES, assume_msg::MAX_MAP_TEXTURES);
	int const new_miptex_num = nummiptex;
	miptex[new_miptex_num].lump_info.name = name;
	++nummiptex;
	ThreadUnlock();
	return new_miptex_num;
}

// =====================================================================================
//  TEX_InitFromWad
// =====================================================================================
static bool TEX_InitFromWad(std::filesystem::path const & bspPath) {
	std::u8string_view pszWadFile;
	char const * pszWadroot;
	wadpath_t* currentwad;

	Log("\n"); // looks cleaner
	// update wad inclusion
	for (std::size_t i = 0; i < g_pWadPaths.size();
	     ++i) // loop through all wadpaths in map
	{
		currentwad = g_pWadPaths[i];
		if (!g_wadtextures) // If -nowadtextures used
		{
			currentwad->usedbymap = false;
		}
		for (WadInclude_i it = g_WadInclude.begin();
		     it != g_WadInclude.end();
		     it++) // Check -wadinclude list
		{
			if (a_contains_b_ignoring_ascii_character_case_differences(
					currentwad->path, *it
				)) {
				currentwad->usedbymap = false;
			}
		}
	}

	pszWadroot = getenv("WADROOT");

	// for eachwadpath
	for (std::size_t i = 0; i < g_pWadPaths.size(); ++i) {
		FILE* texfile; // temporary used in this loop
		currentwad = g_pWadPaths[i];
		pszWadFile = currentwad->path;
		texwadpathes[nTexFiles] = currentwad;
		texfiles[nTexFiles] = fopen((char const *) pszWadFile.data(), "rb");

		if (!texfiles[nTexFiles] && pszWadroot) {
			char szTmp[_MAX_PATH];
			char szFile[_MAX_PATH];
			char szSubdir[_MAX_PATH];

			ExtractFile((char const *) pszWadFile.data(), szFile);

			ExtractFilePath((char const *) pszWadFile.data(), szTmp);
			ExtractFile(szTmp, szSubdir);

			// szSubdir will have a trailing separator
			safe_snprintf(
				szTmp, _MAX_PATH, "%s/%s%s", pszWadroot, szSubdir, szFile
			);
			texfiles[nTexFiles] = fopen(szTmp, "rb");

#ifdef SYSTEM_POSIX
			if (!texfiles[nTexFiles]) {
				// if we cant find it, Convert to lower case and try again
				std::u8string lowercase
					= ascii_characters_to_lowercase_in_utf8_string(
						(char8_t const *) szTmp
					);
				texfiles[nTexFiles] = fopen(
					(char const *) lowercase.c_str(), "rb"
				);
			}
#endif
		}

		if (!texfiles[nTexFiles]) {
			// Look in the mod dir
			std::filesystem::path const modDir
				= bspPath.parent_path().parent_path();
			std::filesystem::path const wadFilename
				= filename_in_file_path_string(pszWadFile);
			auto wadInModDir = modDir / wadFilename;
			texfiles[nTexFiles] = fopen(
				(char const *) wadInModDir.c_str(), "rb"
			);
		}

		if (!texfiles[nTexFiles]) {
			// still cant find it, error out
			Fatal(
				assume_msg::COULD_NOT_FIND_WAD,
				"Could not open wad file %s",
				(char const *) pszWadFile.data()
			);
			continue;
		}

		// temp assignment to make things cleaner:
		texfile = texfiles[nTexFiles];

		wadinfo_t wadinfo;
		// read in this wadfiles information
		SafeRead(texfile, &wadinfo, sizeof(wadinfo));

		// make sure its a valid format
		if (!has_wad_identification(wadinfo)) {
			Log(" - ");
			Error("%s isn't a Wadfile!", (char const *) pszWadFile.data());
		}

		wadinfo.numlumps = (wadinfo.numlumps);
		wadinfo.infotableofs = (wadinfo.infotableofs);

		// read in lump
		if (fseek(texfile, wadinfo.infotableofs, SEEK_SET)) {
			Warning(
				"fseek to %d in wadfile %s failed\n",
				wadinfo.infotableofs,
				(char const *) pszWadFile.data()
			);
		}

		// memalloc for this lump
		lumpinfo = (lumpinfo_with_wadfileindex*) realloc(
			lumpinfo,
			(nTexLumps + wadinfo.numlumps)
				* sizeof(lumpinfo_with_wadfileindex)
		);

		// for each texlump
		std::vector<std::tuple<std::string, char*, int>> texturesOversized;

		for (std::size_t j = 0; j < wadinfo.numlumps; ++j, ++nTexLumps) {
			SafeRead(
				texfile,
				&lumpinfo[nTexLumps].lump_info,
				sizeof(wad_lumpinfo)
			); // iTexFile is NOT read from file
			char szWadFileName[_MAX_PATH];
			ExtractFile((char const *) pszWadFile.data(), szWadFileName);

			if (!lumpinfo[nTexLumps].lump_info.name.validate_and_normalize(
				)) {
				Error(
					"Texture number %zu has a bad name. The WAD file %s has been corrupted or it was generated by a program that either incorrectly tries to write texture names longer than 15 code units or writes non-UTF-8 names.",
					j,
					szWadFileName
				);
			}
			lumpinfo[nTexLumps].lump_info.filepos
				= (lumpinfo[nTexLumps].lump_info.filepos);
			lumpinfo[nTexLumps].lump_info.disksize
				= (lumpinfo[nTexLumps].lump_info.disksize);
			lumpinfo[nTexLumps].iTexFile = nTexFiles;

			if (lumpinfo[nTexLumps].lump_info.disksize > MAX_TEXTURE_SIZE) {
				texturesOversized.push_back(std::make_tuple(
					(char const *) lumpinfo[nTexLumps]
						.lump_info.name.string_view()
						.data(),
					szWadFileName,
					lumpinfo[nTexLumps].lump_info.disksize
				));
			}
		}
		if (!texturesOversized.empty()) {
			Warning("potentially oversized textures detected");
			Log("If map doesn't run, -wadinclude the following\n");
			Log("Wadinclude may support resolutions up to 544*544\n");
			Log("------------------------------------------------\n");

			for (auto const & texture : texturesOversized) {
				std::string const & texName = std::get<0>(texture);
				char* szWadFileName = std::get<1>(texture);
				int texBytes = std::get<2>(texture);
				Log("[%s] %s (%d bytes)\n",
				    szWadFileName,
				    texName.c_str(),
				    texBytes);
			}
			Log("----------------------------------------------------\n");
		}

		// This feature is dependant on autowad. :(
		// CONSIDER: making it standard?
		currentwad->totaltextures = wadinfo.numlumps;

		nTexFiles++;
		hlassume(
			nTexFiles < MAX_TEXFILES, assume_msg::exceeded_MAX_TEXFILES
		);
	}

	// Log("num of used textures: %i\n", g_numUsedTextures);

	// sort texlumps in memory by name
	qsort(
		(void*) lumpinfo,
		(size_t) nTexLumps,
		sizeof(lumpinfo[0]),
		lump_sorter_by_name
	);

	CheckFatal();
	return true;
}

// =====================================================================================
//  FindTexture
// =====================================================================================
lumpinfo_with_wadfileindex*
FindTexture(lumpinfo_with_wadfileindex const * const source) {
	// Log("** PnFNFUNC: FindTexture\n");

	lumpinfo_with_wadfileindex* found = nullptr;

	found = (lumpinfo_with_wadfileindex*) bsearch(
		source,
		(void*) lumpinfo,
		(size_t) nTexLumps,
		sizeof(lumpinfo[0]),
		lump_sorter_by_name
	);
	if (!found) {
		Warning(
			"::FindTexture() texture %s not found!",
			source->lump_info.name.c_str()
		);
		// TODO: Check for all special textures included in hlt.wad?
		if (source->lump_info.name.is_ordinary_null()
		    || source->lump_info.name.is_skip()) {
			Log("Are you sure you included hlt.wad in your wadpath list?\n"
			);
		}
	}

	if (found) {
		// get the first and last matching lump
		lumpinfo_with_wadfileindex* first = found;
		lumpinfo_with_wadfileindex* last = found;
		while (first - 1 >= lumpinfo
		       && lump_sorter_by_name(first - 1, source) == 0) {
			first = first - 1;
		}
		while (last + 1 < lumpinfo + nTexLumps
		       && lump_sorter_by_name(last + 1, source) == 0) {
			last = last + 1;
		}
		// find the best matching lump
		lumpinfo_with_wadfileindex* best = nullptr;
		for (found = first; found < last + 1; found++) {
			bool better = false;
			if (best == nullptr) {
				better = true;
			} else if (found->iTexFile != best->iTexFile) {
				wadpath_t* found_wadpath = texwadpathes[found->iTexFile];
				wadpath_t* best_wadpath = texwadpathes[best->iTexFile];
				if (found_wadpath->usedbymap != best_wadpath->usedbymap) {
					better = !found_wadpath
								  ->usedbymap; // included wad is better
				} else {
					better = found->iTexFile
						< best->iTexFile; // upper in the wad list is better
				}
			} else if (found->lump_info.filepos
			           != best->lump_info.filepos) {
				better = found->lump_info.filepos
					< best->lump_info
						  .filepos; // when there are several lumps with the
				                    // same name in one wad file
			}

			if (better) {
				best = found;
			}
		}
		found = best;
	}
	return found;
}

// =====================================================================================
//  LoadLump
// =====================================================================================
static int LoadLump(
	lumpinfo_with_wadfileindex const * const source,
	std::byte* dest,
	int* texsize,
	int dest_maxsize,
	std::byte*& writewad_data,
	int& writewad_datasize
) {
	writewad_data = nullptr;
	writewad_datasize = -1;
	// Log("** PnFNFUNC: LoadLump\n");

	*texsize = 0;
	if (source->lump_info.filepos) {
		if (fseek(
				texfiles[source->iTexFile],
				source->lump_info.filepos,
				SEEK_SET
			)) {
			Warning("fseek to %d failed\n", source->lump_info.filepos);
			Error("LoadLump: File read failure");
		}
		*texsize = source->lump_info.disksize;

		if (texwadpathes[source->iTexFile]->usedbymap) {
			// Just read the miptex header and zero out the data offsets.
			// We will load the entire texture from the WAD at engine
			// runtime
			int i;
			miptex_t* miptex = (miptex_t*) dest;
			hlassume(
				(int) sizeof(miptex_t) <= dest_maxsize,
				assume_msg::MAX_MAP_MIPTEX
			);
			SafeRead(texfiles[source->iTexFile], dest, sizeof(miptex_t));

			if (!miptex->name.validate_and_normalize()) {
				// TODO: Better error message
				Error("validate_and_normalize failed");
			}

			for (i = 0; i < MIPLEVELS; i++) {
				miptex->offsets[i] = 0;
			}
			writewad_data = (std::byte*) malloc(source->lump_info.disksize);
			hlassume(writewad_data != nullptr, assume_msg::NoMemory);
			if (fseek(
					texfiles[source->iTexFile],
					source->lump_info.filepos,
					SEEK_SET
				)) {
				Error("File read failure");
			}
			SafeRead(
				texfiles[source->iTexFile],
				writewad_data,
				source->lump_info.disksize
			);
			writewad_datasize = source->lump_info.disksize;
			return sizeof(miptex_t);
		} else {
			Developer(
				developer_level::message,
				"Including texture %s\n",
				source->lump_info.name.c_str()
			);

			// Load the entire texture here so the BSP contains the texture
			hlassume(
				source->lump_info.disksize <= dest_maxsize,
				assume_msg::MAX_MAP_MIPTEX
			);
			SafeRead(
				texfiles[source->iTexFile], dest, source->lump_info.disksize
			);

			if (!((miptex_t*) dest)->name.validate_and_normalize()) {
				// TODO: Better error message
				Error("validate_and_normalize failed");
			}
			return source->lump_info.disksize;
		}
	}

	Error(
		"::LoadLump() texture %s not found!", source->lump_info.name.c_str()
	);

	return 0;
}

// =====================================================================================
//  AddAnimatingTextures
// =====================================================================================
void AddAnimatingTextures() {
	int const base = nummiptex;
	for (std::size_t i = 0; i < base; i++) {
		wad_texture_name name{ miptex[i].lump_info.name };
		if (!name.is_animation_frame() && !name.is_tile()) {
			continue;
		}

		for (std::size_t j = 0; j < 10; ++j) {
			for (bool alternateAnimation : std::array{ false, true }) {
				name.set_animation_frame_or_tile_number(
					j, alternateAnimation
				);
				// See if this name exists in the wadfile
				for (std::size_t k = 0; k < nTexLumps; ++k) {
					if (name == lumpinfo[k].lump_info.name) {
						FindMiptex(name); // Add to the miptex list
						break;
					}
				}
			}
		}
	}

	if (nummiptex - base) {
		Log("Added %i additional animating textures.\n", nummiptex - base);
	}
}

// =====================================================================================
//  WriteMiptex
// =====================================================================================
void WriteMiptex(std::filesystem::path const & bspPath) {
	int len, texsize, totaltexsize = 0;
	std::byte* data;
	dmiptexlump_t* l;

	g_texdatasize = 0;

	time_counter timeCounter;

	if (!TEX_InitFromWad(bspPath)) {
		return;
	}

	AddAnimatingTextures();

	Verbose(
		"TEX_InitFromWad & AddAnimatingTextures elapsed time = %.8fs\n",
		timeCounter.get_total()
	);

	timeCounter.restart();
	{
		for (int i = 0; i < nummiptex; ++i) {
			lumpinfo_with_wadfileindex* found;

			found = FindTexture(miptex + i);
			if (found) {
				miptex[i] = *found;
				texwadpathes[found->iTexFile]->usedtextures++;
			} else {
				miptex[i].iTexFile = miptex[i].lump_info.filepos
					= miptex[i].lump_info.disksize = 0;
			}
		}
	}
	Verbose("FindTextures elapsed time = %.8fs\n", timeCounter.get_total());

	// Now we have filled lumpinfo for each miptex and the number of used
	// textures for each wad.
	{
		char szUsedWads[MAX_VAL];
		int i;

		szUsedWads[0] = 0;
		std::vector<wadpath_t*> usedWads;
		std::vector<wadpath_t*> includedWads;

		for (i = 0; i < nTexFiles; i++) {
			wadpath_t* currentwad = texwadpathes[i];
			if (currentwad->usedbymap && (currentwad->usedtextures > 0)) {
				char tmp[_MAX_PATH];
				ExtractFile((char const *) currentwad->path.c_str(), tmp);
				safe_strncat(szUsedWads, tmp, MAX_VAL); // Concat wad names
				safe_strncat(szUsedWads, ";", MAX_VAL);
				usedWads.push_back(currentwad);
			}
		}
		for (i = 0; i < nTexFiles; i++) {
			wadpath_t* currentwad = texwadpathes[i];
			if (!currentwad->usedbymap && (currentwad->usedtextures > 0)) {
				includedWads.push_back(currentwad);
			}
		}
		if (!usedWads.empty()) {
			Log("Wad files used by map\n");
			Log("---------------------\n");
			for (std::vector<wadpath_t*>::iterator it = usedWads.begin();
			     it != usedWads.end();
			     ++it) {
				wadpath_t* currentwad = *it;
				LogWadUsage(currentwad, nummiptex);
			}
			Log("---------------------\n\n");
		} else {
			Log("No wad files used by the map\n");
		}
		if (!includedWads.empty()) {
			Log("Additional wad files included\n");
			Log("-----------------------------\n");

			for (std::vector<wadpath_t*>::iterator it = includedWads.begin(
				 );
			     it != includedWads.end();
			     ++it) {
				wadpath_t* currentwad = *it;
				LogWadUsage(currentwad, nummiptex);
			}
			Log("-----------------------------\n\n");
		} else {
			Log("No additional wad files included\n\n");
		}
		set_key_value(
			&g_entities[0], u8"wad", (char8_t const *) szUsedWads
		);
	}

	timeCounter.restart();
	{
		texinfo_t* tx = g_texinfo.data();

		// Sort them FIRST by wadfile and THEN by name for most efficient
		// loading in the engine.
		qsort(
			(void*) miptex,
			(size_t) nummiptex,
			sizeof(miptex[0]),
			lump_sorter_by_wad_and_name
		);

		// Sleazy Hack 104 Pt 2 - After sorting the miptex array, reset the
		// texinfos to point to the right miptexs
		for (int i = 0; i < g_numtexinfo; i++, tx++) {
			wad_texture_name miptex_name{ texmap_retrieve(tx->miptex) };
			tx->miptex = FindMiptex(miptex_name);
		}
		texmap_clear();
	}
	Verbose(
		"qsort(miptex) elapsed time = %.8fs\n", timeCounter.get_total()
	);

	timeCounter.restart();
	{
		int i;

		// Now setup to get the miptex data (or just the headers if using
		// -wadtextures) from the wadfile
		l = (dmiptexlump_t*) g_dtexdata.data();
		data = (std::byte*) &l->dataofs[nummiptex];
		l->nummiptex = nummiptex;
		FILE* writewad_file;
		int writewad_maxlumpinfos;
		wad_lumpinfo* writewad_lumpinfos;
		wadinfo_t writewad_header;

		// Generate temp wad file with name based on mapname, to contain
		// processed textures
		writewad_file = SafeOpenWrite(
			path_to_temp_file_with_extension(g_Mapname, u8".wa_").c_str()
		);

		// Malloc for storing lump info
		writewad_maxlumpinfos = nummiptex;
		writewad_lumpinfos = (wad_lumpinfo*) malloc(
			writewad_maxlumpinfos * sizeof(wad_lumpinfo)
		);
		hlassume(writewad_lumpinfos != nullptr, assume_msg::NoMemory);

		// Header for the temp wad file
		writewad_header.identification[0] = 'W';
		writewad_header.identification[1] = 'A';
		writewad_header.identification[2] = 'D';
		writewad_header.identification[3] = '3';
		writewad_header.numlumps = 0;

		if (fseek(
				writewad_file, sizeof(wadinfo_t), SEEK_SET
			)) { // Move file pointer to skip header
			Error("File write failure");
		}
		for (i = 0; i < nummiptex; i++) // Process each miptex, writing its
		                                // data to the temp wad file
		{
			l->dataofs[i] = data - (std::byte*) l;
			std::byte* writewad_data;
			int writewad_datasize;
			len = LoadLump(
				miptex + i,
				data,
				&texsize,
				&g_dtexdata[g_max_map_miptex] - data,
				writewad_data,
				writewad_datasize
			); // Load lump data

			if (writewad_data) {
				// Prepare lump info for temp wad file
				wad_lumpinfo* writewad_lumpinfo
					= &writewad_lumpinfos[writewad_header.numlumps];
				writewad_lumpinfo->filepos = ftell(writewad_file);
				writewad_lumpinfo->disksize = writewad_datasize;
				writewad_lumpinfo->size = miptex[i].lump_info.size;
				writewad_lumpinfo->type = miptex[i].lump_info.type;
				writewad_lumpinfo->compression
					= miptex[i].lump_info.compression;
				writewad_lumpinfo->pad1 = miptex[i].lump_info.pad1;
				writewad_lumpinfo->pad2 = miptex[i].lump_info.pad2;
				writewad_lumpinfo->name = miptex[i].lump_info.name;

				writewad_header.numlumps++;
				SafeWrite(
					writewad_file, writewad_data, writewad_datasize
				); // Write the processed lump info temp wad file
				free(writewad_data);
			}

			if (!len) {
				l->dataofs[i] = -1; // Mark texture not found
			} else {
				totaltexsize += texsize;

				hlassume(
					totaltexsize < g_max_map_miptex,
					assume_msg::MAX_MAP_MIPTEX
				);
			}
			data += len;
		}
		g_texdatasize = data - g_dtexdata.data();
		// Write lump info and header to the temp wad file
		writewad_header.infotableofs = ftell(writewad_file);
		SafeWrite(
			writewad_file,
			writewad_lumpinfos,
			writewad_header.numlumps * sizeof(wad_lumpinfo)
		);
		if (fseek(writewad_file, 0, SEEK_SET)) {
			Error("File write failure");
		}
		SafeWrite(writewad_file, &writewad_header, sizeof(wadinfo_t));
		if (fclose(writewad_file)) {
			Error("File write failure");
		}
	}
	Log("Texture usage: %1.2f/%1.2f MB)\n",
	    (float) totaltexsize / (1024 * 1024),
	    (float) g_max_map_miptex / (1024 * 1024));
	Verbose("LoadLump() elapsed time: %.8fs\n", timeCounter.get_total());
}

// =====================================================================================
//  LogWadUsage
// =====================================================================================
void LogWadUsage(wadpath_t* currentwad, int nummiptex) {
	if (currentwad == nullptr) {
		return;
	}
	char currentwadName[_MAX_PATH];
	ExtractFile((char const *) currentwad->path.c_str(), currentwadName);
	double percentUsed = (double) currentwad->usedtextures
		/ (double) nummiptex * 100;

	Log("[%s] %i/%i texture%s (%2.2f%%)\n - %s\n",
	    currentwadName,
	    currentwad->usedtextures,
	    currentwad->totaltextures,
	    currentwad->usedtextures == 1 ? "" : "s",
	    percentUsed,
	    (char const *) currentwad->path.c_str());
}

texinfo_count TexinfoForBrushTexture(
	mapplane_t const * const plane,
	brush_texture_t* bt,
	double3_array const & origin
) {
	wad_texture_name textureName{ bt->name };

	if (textureName.is_ordinary_null()) {
		return -1;
	}
	texinfo_t tx{};
	FindMiptex(bt->name);
	textureName = bt->name;

	// Set the special flag
	if (textureName.is_ordinary_sky() || textureName.is_env_sky()
	    || textureName.is_origin() || textureName.is_ordinary_null()
	    || textureName.is_aaatrigger()) {
		// actually only 'sky' and 'aaatrigger' needs this. --vluzacn
		tx.set_special_flag(true);
	}

	if (!bt->vects.scale[0]) {
		bt->vects.scale[0] = 1;
	}
	if (!bt->vects.scale[1]) {
		bt->vects.scale[1] = 1;
	}

	double scale = 1 / bt->vects.scale[0];
	tx.vecs[0].xyz = to_float3(vector_scale(bt->vects.UAxis, scale));

	scale = 1 / bt->vects.scale[1];
	tx.vecs[1].xyz = to_float3(vector_scale(bt->vects.VAxis, scale));

	tx.vecs[0].offset = bt->vects.shift[0]
		+ dot_product(origin, tx.vecs[0].xyz);
	tx.vecs[1].offset = bt->vects.shift[1]
		+ dot_product(origin, tx.vecs[1].xyz);

	//
	// find the g_texinfo
	//
	ThreadLock();

	texinfo_t* tc = g_texinfo.data();
	for (int i = 0; i < g_numtexinfo; ++i, ++tc) {
		if (texmap_retrieve(tc->miptex) != bt->name) {
			continue;
		}
		if (tc->flags != tx.flags) {
			continue;
		}
		if (tc->vecs == tx.vecs) {
			ThreadUnlock();
			return i;
		}
	}

	hlassume(
		g_numtexinfo < INITIAL_MAX_MAP_TEXINFO,
		assume_msg::INITIAL_MAX_MAP_TEXINFO
	);

	*tc = tx;
	tc->miptex = texmap_store(bt->name);
	texinfo_count const newTexinfo = g_numtexinfo++;
	ThreadUnlock();
	return newTexinfo;
}

// Before WriteMiptex(), for each texinfo in g_texinfo, .miptex is a string
// rather than texture index, so this function should be used instead of
// GetTextureByNumber.
std::optional<wad_texture_name>
GetTextureByNumber_CSG(texinfo_count texturenumber) {
	if (texturenumber == no_texinfo) {
		return std::nullopt;
	}
	return texmap_retrieve(g_texinfo[texturenumber].miptex);
}
