/***
 *
 *	Copyright (c) 1998, Valve LLC. All rights reserved.
 *
 *	This product contains software technology licensed from Id
 *	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software,
 *Inc. All Rights Reserved.
 *
 ****/

// csg4.c

#include "ripent.h"

#include "bsp_file_sizes.h"
#include "cli_option_defaults.h"
#include "time_counter.h"
#include "wad_structs.h"

#include <cstring>
#include <filesystem>

enum hl_types {
	hl_undefined = -1,
	hl_export = 0,
	hl_import = 1
};

static hl_types g_mode = hl_undefined;
static hl_types g_texturemode = hl_undefined;

// g_parse: command line switch (-parse).
// Added by: Ryan Gregg aka Nem
bool g_parse = DEFAULT_PARSE;
bool g_textureparse = DEFAULT_TEXTUREPARSE;

bool g_chart = cli_option_defaults::chart;

bool g_info = cli_option_defaults::info;

bool g_writeextentfile = DEFAULT_WRITEEXTENTFILE;

bool g_deleteembeddedlightmaps = DEFAULT_DELETEEMBEDDEDLIGHTMAPS;

// ScanForToken()
// Added by: Ryan Gregg aka Nem
//
// Scans entity data starting  at iIndex for cToken.  Every time a \n char
// is encountered iLine is incremented.  If iToken is not null, the index
// cToken was found at is inserted into it.
bool ScanForToken(
	char cToken,
	int& iIndex,
	int& iLine,
	bool bIgnoreWhiteSpace,
	bool bIgnoreOthers,
	int* iToken = 0
) {
	for (; iIndex < g_entdatasize; iIndex++) {
		// If we found a null char, consider it end of data.
		if (g_dentdata[iIndex] == u8'\0') {
			iIndex = g_entdatasize;
			return false;
		}

		// Count lines (for error message).
		if (g_dentdata[iIndex] == u8'\n') {
			iLine++;
		}

		// Ignore white space, if we are ignoring it.
		if (!bIgnoreWhiteSpace && is_ascii_whitespace(g_dentdata[iIndex])) {
			continue;
		}

		if (g_dentdata[iIndex] != cToken) {
			if (bIgnoreOthers) {
				continue;
			} else {
				return false;
			}
		}

		// Advance the index past the token.
		iIndex++;

		// Return the index of the token if requested.
		if (iToken != 0) {
			*iToken = iIndex - 1;
		}

		return true;
	}

	// End of data.
	return false;
}

#include <list>
using CEntityPairList = std::list<char*>;
using CEntityList = std::list<CEntityPairList*>;

// ParseEntityData()
// Added by: Ryan Gregg aka Nem
//
// Pareses and reformats entity data stripping all non essential
// formatting  and using the formatting  options passed through this
// function.  The length is specified because in some cases (i.e. the
// terminator) a null char is desired to be printed.
void ParseEntityData(
	char const * cTab,
	int iTabLength,
	char const * cNewLine,
	int iNewLineLength,
	char const * cTerminator,
	int iTerminatorLength
) {
	CEntityList EntityList; // Parsed entities.

	int iIndex = 0; // Current char in g_dentdata.
	int iLine = 0;	// Current line in g_dentdata.

	char cError[256] = "";

	try {
		//
		// Parse entity data.
		//

		Log("\nParsing entity data.\n");

		while (true) {
			// Parse the start of an entity.
			if (!ScanForToken('{', iIndex, iLine, false, false)) {
				if (iIndex == g_entdatasize) {
					// We read all the entities.
					break;
				} else {
					snprintf(
						cError,
						sizeof(cError),
						"expected token %s on line %d.",
						"{",
						iLine
					);
					throw cError;
				}
			}

			CEntityPairList* EntityPairList = new CEntityPairList();

			// Parse the rest of the entity.
			while (true) {
				// Parse the key and value.
				for (int j = 0; j < 2; j++) {
					int iStart;
					// Parse the start of a string.
					if (!ScanForToken(
							'\"', iIndex, iLine, false, false, &iStart
						)) {
						snprintf(
							cError,
							sizeof(cError),
							"expected token %s on line %d.",
							"\"",
							iLine
						);
						throw cError;
					}

					int iEnd;
					// Parse the end of a string.
					if (!ScanForToken(
							'\"', iIndex, iLine, true, true, &iEnd
						)) {
						snprintf(
							cError,
							sizeof(cError),
							"expected token %s on line %d.",
							"\"",
							iLine
						);
						throw cError;
					}

					// Extract the string.
					int iLength = iEnd - iStart - 1;
					char* cString = new char[iLength + 1];
					std::memcpy(cString, &g_dentdata[iStart + 1], iLength);
					cString[iLength] = '\0';

					// Save it.
					EntityPairList->push_back(cString);
				}

				// Parse the end of an entity.
				if (!ScanForToken('}', iIndex, iLine, false, false)) {
					if (g_dentdata[iIndex] == u8'\"') {
						// We arn't done the entity yet.
						continue;
					} else {
						snprintf(
							cError,
							sizeof(cError),
							"expected token %s on line %d.",
							"}",
							iLine
						);
						throw cError;
					}
				}

				// We read the entity.
				EntityList.push_back(EntityPairList);
				break;
			}
		}

		Log("%d entities parsed.\n", (int) EntityList.size());

		//
		// Calculate new data length.
		//

		int iNewLength = 0;

		for (CEntityList::iterator i = EntityList.begin();
			 i != EntityList.end();
			 ++i) {
			// Opening brace.
			iNewLength += 1;

			// New line.
			iNewLength += iNewLineLength;

			CEntityPairList* EntityPairList = *i;

			for (CEntityPairList::iterator j = EntityPairList->begin();
				 j != EntityPairList->end();
				 ++j) {
				// Tab.
				iNewLength += iTabLength;

				// String.
				iNewLength += 1;
				iNewLength += (int) strlen(*j);
				iNewLength += 1;

				// String seperator.
				iNewLength += 1;

				++j;

				// String.
				iNewLength += 1;
				iNewLength += (int) strlen(*j);
				iNewLength += 1;

				// New line.
				iNewLength += iNewLineLength;
			}

			// Closing brace.
			iNewLength += 1;

			// New line.
			iNewLength += iNewLineLength;
		}

		// Terminator.
		iNewLength += iTerminatorLength;

		//
		// Check our parsed data.
		//

		assume(iNewLength != 0, "No entity data.");
		assume(
			iNewLength < sizeof(g_dentdata),
			"Entity data size exceedes dentdata limit."
		);

		//
		// Clear current data.
		//

		g_entdatasize = 0;

		//
		// Fill new data.
		//

		Log("Formating entity data.\n\n");

		for (CEntityList::iterator i = EntityList.begin();
			 i != EntityList.end();
			 ++i) {
			// Opening brace.
			g_dentdata[g_entdatasize] = u8'{';
			g_entdatasize += 1;

			// New line.
			std::memcpy(
				&g_dentdata[g_entdatasize], cNewLine, iNewLineLength
			);
			g_entdatasize += iNewLineLength;

			CEntityPairList* EntityPairList = *i;

			for (CEntityPairList::iterator j = EntityPairList->begin();
				 j != EntityPairList->end();
				 ++j) {
				// Tab.
				std::memcpy(&g_dentdata[g_entdatasize], cTab, iTabLength);
				g_entdatasize += iTabLength;

				// String.
				g_dentdata[g_entdatasize] = u8'\"';
				g_entdatasize += 1;
				std::memcpy(&g_dentdata[g_entdatasize], *j, strlen(*j));
				g_entdatasize += (int) strlen(*j);
				g_dentdata[g_entdatasize] = u8'\"';
				g_entdatasize += 1;

				// String seperator.
				g_dentdata[g_entdatasize] = u8' ';
				g_entdatasize += 1;

				++j;

				// String.
				g_dentdata[g_entdatasize] = u8'\"';
				g_entdatasize += 1;
				std::memcpy(&g_dentdata[g_entdatasize], *j, strlen(*j));
				g_entdatasize += (int) strlen(*j);
				g_dentdata[g_entdatasize] = u8'\"';
				g_entdatasize += 1;

				// New line.
				std::memcpy(
					&g_dentdata[g_entdatasize], cNewLine, iNewLineLength
				);
				g_entdatasize += iNewLineLength;
			}

			// Closing brace.
			g_dentdata[g_entdatasize] = u8'}';
			g_entdatasize += 1;

			// New line.
			std::memcpy(
				&g_dentdata[g_entdatasize], cNewLine, iNewLineLength
			);
			g_entdatasize += iNewLineLength;
		}

		// Terminator.
		std::memcpy(
			&g_dentdata[g_entdatasize], cTerminator, iTerminatorLength
		);
		g_entdatasize += iTerminatorLength;

		//
		// Delete entity data.
		//

		for (CEntityList::iterator i = EntityList.begin();
			 i != EntityList.end();
			 ++i) {
			CEntityPairList* EntityPairList = *i;

			for (CEntityPairList::iterator j = EntityPairList->begin();
				 j != EntityPairList->end();
				 ++j) {
				delete[] *j;
			}

			delete EntityPairList;
		}

		// return true;
	} catch (...) {
		//
		// Delete entity data.
		//

		for (CEntityList::iterator i = EntityList.begin();
			 i != EntityList.end();
			 ++i) {
			CEntityPairList* EntityPairList = *i;

			for (CEntityPairList::iterator j = EntityPairList->begin();
				 j != EntityPairList->end();
				 ++j) {
				delete[] *j;
			}

			delete EntityPairList;
		}

		// If we threw the error cError wont be null, this is
		// a message, print it.
		if (*cError != '\0') {
			Error("%s", cError);
		}
		Error("Unknown exception.");

		// return false;
	}
}

static void ReadBSP(char const * const name) {
	std::filesystem::path bspPath;
	bspPath = name;
	bspPath += u8".bsp";

	LoadBSPFile(bspPath);
	if (g_writeextentfile) {
		hlassume(CalcFaceExtents_test(), assume_first);
		std::filesystem::path extentFilePath;
		extentFilePath = name;
		extentFilePath += u8".ext";
		Log("\nWriting %s.\n", extentFilePath.c_str());
		WriteExtentFile(extentFilePath);
	}
}

static void WriteBSP(char const * const name) {
	std::filesystem::path filename;
	filename = name;
	filename += u8".bsp";

	Log("\nUpdating %s.\n", filename.c_str()); //--vluzacn
	WriteBSPFile(filename);
}

/*int TextureSize(const miptex_t *tex)
{
	int size = 0;
	int w, h;
	size += sizeof(miptex_t);
	w = tex->width, h = tex->height;
	for (int imip = 0; imip < MIPLEVELS; ++imip, w/=2, h/=2)
	{
		size += w * h;
	}
	size += 256 * 3 + 4;
	return size;
}
void WriteEmptyTexture(FILE *outwad, const miptex_t *tex)
{
	miptex_t outtex;
	int start, end;
	int w, h;
	memcpy (&outtex, tex, sizeof(miptex_t));
	start = ftell (outwad);
	fseek (outwad, sizeof(miptex_t), SEEK_CUR);
	w = tex->width, h = tex->height;
	for (int imip = 0; imip < MIPLEVELS; ++imip, w/=2, h/=2)
	{
		void *tmp = calloc (w * h, 1);
		outtex.offsets[imip] = ftell (outwad) - start;
		SafeWrite (outwad, tmp, w * h);
		free (tmp);
	}
	short s = 256;
	SafeWrite (outwad, &s, sizeof(short));
	void *tmp = calloc (256 * 3 + 2, 1); // assume width and height are
multiples of 16 SafeWrite (outwad, tmp, 256 * 3 + 2); free (tmp); end =
ftell (outwad); fseek (outwad, start, SEEK_SET); SafeWrite (outwad, &outtex,
sizeof (miptex_t)); fseek (outwad, end, SEEK_SET);
}*/
static void WriteTextures(char const * const name) {
	char wadfilename[_MAX_PATH];
	FILE* wadfile;
	safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
	std::filesystem::remove(wadfilename);
	wadfile = SafeOpenWrite(wadfilename);
	Log("\nWriting %s.\n", wadfilename);

	char texfilename[_MAX_PATH];
	FILE* texfile;
	safe_snprintf(texfilename, _MAX_PATH, "%s.tex", name);
	std::filesystem::remove(texfilename);
	if (!g_textureparse) {
		int dataofs = (int) (intptr_t)
			& ((dmiptexlump_t*) NULL)
				  ->dataofs[((dmiptexlump_t*) g_dtexdata.data())
								->nummiptex];
		int wadofs = sizeof(wadinfo_t);

		wadinfo_t header;
		header.identification[0] = 'W';
		header.identification[1] = 'A';
		header.identification[2] = 'D';
		header.identification[3] = '3';
		header.numlumps = ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
		header.infotableofs = g_texdatasize - dataofs + wadofs;
		SafeWrite(wadfile, &header, wadofs);

		SafeWrite(
			wadfile,
			(byte*) g_dtexdata.data() + dataofs,
			g_texdatasize - dataofs
		);

		wad_lumpinfo* info;
		info = (wad_lumpinfo*) malloc(
			((dmiptexlump_t*) g_dtexdata.data())->nummiptex
			* sizeof(wad_lumpinfo)
		);
		hlassume(info != nullptr, assume_NoMemory);
		std::fill_n(info, header.numlumps, wad_lumpinfo{});

		for (int i = 0; i < header.numlumps; i++) {
			int ofs = ((dmiptexlump_t*) g_dtexdata.data())->dataofs[i];
			int size = 0;
			if (ofs >= 0) {
				size = g_texdatasize - ofs;
				for (int j = 0;
					 j < ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
					 ++j) {
					if (ofs < ((dmiptexlump_t*) g_dtexdata.data())
								  ->dataofs[j]
						&& ofs + size > ((dmiptexlump_t*) g_dtexdata.data())
											->dataofs[j]) {
						size = ((dmiptexlump_t*) g_dtexdata.data())
								   ->dataofs[j]
							- ofs;
					}
				}
			}
			info[i].filepos = ofs - dataofs + wadofs;
			info[i].disksize = size;
			info[i].size = size;
			info[i].type
				= (ofs >= 0
				   && ((miptex_t*) (g_dtexdata.data() + ofs))->offsets[0]
					   > 0)
				? 67
				: 0; // prevent invalid texture from being processed by
					 // Wally
			info[i].compression = 0;
			info[i].name = ofs >= 0
				? wad_texture_name{ ((miptex_t*) (g_dtexdata.data() + ofs))
										->name }
				: wad_texture_name{ u8"texturemissing" };
		}
		SafeWrite(wadfile, info, header.numlumps * sizeof(wad_lumpinfo));
		free(info);
	} else {
		texfile = SafeOpenWrite(texfilename);
		Log("\nWriting %s.\n", texfilename);

		wadinfo_t header;
		header.identification[0] = 'W';
		header.identification[1] = 'A';
		header.identification[2] = 'D';
		header.identification[3] = '3';
		header.numlumps = 0;

		wad_lumpinfo* info;
		info = (wad_lumpinfo*) malloc(
			((dmiptexlump_t*) g_dtexdata.data())->nummiptex
			* sizeof(wad_lumpinfo)
		); // might be more than needed
		hlassume(info != nullptr, assume_NoMemory);

		fprintf(
			texfile,
			"%d\r\n",
			((dmiptexlump_t*) g_dtexdata.data())->nummiptex
		);
		fseek(wadfile, sizeof(wadinfo_t), SEEK_SET);

		for (int itex = 0;
			 itex < ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
			 ++itex) {
			int ofs = ((dmiptexlump_t*) g_dtexdata.data())->dataofs[itex];
			miptex_t* tex = (miptex_t*) (g_dtexdata.data() + ofs);
			if (ofs < 0) {
				fprintf(texfile, "[-1]\r\n");
			} else {
				int size = g_texdatasize - ofs;
				for (int j = 0;
					 j < ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
					 ++j) {
					if (ofs < ((dmiptexlump_t*) g_dtexdata.data())
								  ->dataofs[j]
						&& ofs + size > ((dmiptexlump_t*) g_dtexdata.data())
											->dataofs[j]) {
						size = ((dmiptexlump_t*) g_dtexdata.data())
								   ->dataofs[j]
							- ofs;
					}
				}
				bool included = false;
				if (tex->offsets[0] > 0) {
					included = true;
				}
				if (included) {
					info[header.numlumps] = wad_lumpinfo{};
					info[header.numlumps].filepos = ftell(wadfile);
					SafeWrite(wadfile, tex, size);
					info[header.numlumps].disksize = ftell(wadfile)
						- info[header.numlumps].filepos;
					info[header.numlumps].size
						= info[header.numlumps].disksize;
					info[header.numlumps].type = 67;
					info[header.numlumps].compression = 0;
					info[header.numlumps].name = wad_texture_name{
						tex->name
					};

					header.numlumps++;
				}
				fprintf(texfile, "[%zu]", tex->name.length());
				SafeWrite(texfile, tex->name.c_str(), tex->name.length());
				fprintf(texfile, " %d %d\r\n", tex->width, tex->height);
			}
		}
		header.infotableofs = ftell(wadfile);
		SafeWrite(wadfile, info, header.numlumps * sizeof(wad_lumpinfo));
		fseek(wadfile, 0, SEEK_SET);
		SafeWrite(wadfile, &header, sizeof(wadinfo_t));

		fclose(texfile);
		free(info);
	}
	fclose(wadfile);
}

inline void skipspace(FILE* f) {
	fscanf(f, "%*[ \t\r\n]s");
}

inline void skipline(FILE* f) {
	fscanf(f, "%*[^\r\n]s");
}

static void ReadTextures(char const * name) {
	char wadfilename[_MAX_PATH];
	FILE* wadfile;
	safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
	wadfile = SafeOpenRead(wadfilename);
	Log("\nReading %s.\n", wadfilename);

	char texfilename[_MAX_PATH];
	FILE* texfile;
	safe_snprintf(texfilename, _MAX_PATH, "%s.tex", name);
	if (!g_textureparse) {
		wadinfo_t header;
		int wadofs = sizeof(wadinfo_t);
		SafeRead(wadfile, &header, wadofs);
		((dmiptexlump_t*) g_dtexdata.data())->nummiptex = header.numlumps;
		int dataofs = (int) (intptr_t)
			& ((dmiptexlump_t*) NULL)
				  ->dataofs[((dmiptexlump_t*) g_dtexdata.data())
								->nummiptex];
		g_texdatasize = header.infotableofs - wadofs + dataofs;

		SafeRead(
			wadfile,
			(byte*) g_dtexdata.data() + dataofs,
			g_texdatasize - dataofs
		);

		wad_lumpinfo* info;
		info = (wad_lumpinfo*) malloc(
			header.numlumps * sizeof(wad_lumpinfo)
		);
		hlassume(info != nullptr, assume_NoMemory);
		SafeRead(wadfile, info, header.numlumps * sizeof(wad_lumpinfo));

		for (int i = 0; i < header.numlumps; i++) {
			((dmiptexlump_t*) g_dtexdata.data())->dataofs[i]
				= info[i].filepos - wadofs + dataofs;
		}

		free(info);
	} else {
		texfile = SafeOpenRead(texfilename);
		Log("\nReading %s.\n", texfilename);

		wadinfo_t header;
		SafeRead(wadfile, &header, sizeof(wadinfo_t));
		fseek(wadfile, header.infotableofs, SEEK_SET);

		wad_lumpinfo* info;
		info = (wad_lumpinfo*) malloc(
			header.numlumps * sizeof(wad_lumpinfo)
		);
		hlassume(info != nullptr, assume_NoMemory);
		SafeRead(wadfile, info, header.numlumps * sizeof(wad_lumpinfo));

		int nummiptex = 0;
		if (skipspace(texfile), fscanf(texfile, "%d", &nummiptex) != 1) {
			Error("File read failure");
		}
		((dmiptexlump_t*) g_dtexdata.data())->nummiptex = nummiptex;
		g_texdatasize = (std::byte*) (&((dmiptexlump_t*) g_dtexdata.data())
										   ->dataofs[nummiptex])
			- g_dtexdata.data();

		for (int itex = 0; itex < nummiptex; ++itex) {
			int len;
			if (skipspace(texfile), fscanf(texfile, "[%d]", &len) != 1) {
				Error("File read failure");
			}
			if (len < 0) {
				((dmiptexlump_t*) g_dtexdata.data())->dataofs[itex] = -1;
			} else {
				std::array<char8_t, 16> rawName{};
				if (len > 15) {
					Error("Texture name is too long");
				}
				SafeRead(texfile, rawName.data(), len);
				wad_texture_name name = wad_texture_name{ rawName.data() };

				((dmiptexlump_t*) g_dtexdata.data())->dataofs[itex]
					= g_texdatasize;
				miptex_t* tex = (miptex_t*) (g_dtexdata.data()
											 + g_texdatasize);
				int j;
				for (j = 0; j < header.numlumps; ++j) {
					if (name == info[j].name) {
						break;
					}
				}
				if (j == header.numlumps) {
					int w, h;
					if (skipspace(texfile),
						fscanf(texfile, "%d", &w) != 1) {
						Error("File read failure");
					}
					if (skipspace(texfile),
						fscanf(texfile, "%d", &h) != 1) {
						Error("File read failure");
					}
					g_texdatasize += sizeof(miptex_t);
					hlassume(
						g_texdatasize < g_max_map_miptex,
						assume_MAX_MAP_MIPTEX
					);
					*tex = miptex_t{};
					tex->name = name;
					tex->width = w;
					tex->height = h;
					for (int k = 0; k < MIPLEVELS; k++) {
						tex->offsets[k] = 0;
					}
				} else {
					fseek(wadfile, info[j].filepos, SEEK_SET);
					g_texdatasize += info[j].disksize;
					hlassume(
						g_texdatasize < g_max_map_miptex,
						assume_MAX_MAP_MIPTEX
					);
					SafeRead(wadfile, tex, info[j].disksize);
				}
			}
			skipline(texfile);
		}

		fclose(texfile);
		free(info);
	}
	fclose(wadfile);
}

static void WriteEntities(char const * const name) {
	char* bak_dentdata{};
	std::filesystem::path filePath;
	filePath = name;
	filePath += u8".ent";

	std::filesystem::remove(filePath);

	std::uint32_t const bak_entdatasize{ g_entdatasize };
	if (g_parse) {
		bak_dentdata = (char*) malloc(g_entdatasize);
		hlassume(bak_dentdata != nullptr, assume_NoMemory);
		std::memcpy(bak_dentdata, g_dentdata.data(), g_entdatasize);
		ParseEntityData("  ", 2, "\r\n", 2, "", 0);
	}

	FILE* f = SafeOpenWrite(filePath.c_str());
	Log("\nWriting %s.\n", filePath.c_str());
	SafeWrite(f, g_dentdata.data(), g_entdatasize);
	fclose(f);
	if (g_parse) {
		g_entdatasize = bak_entdatasize;
		std::memcpy(g_dentdata.data(), bak_dentdata, bak_entdatasize);
		free(bak_dentdata);
	}
}

static void ReadEntities(char const * const name) {
	char filename[_MAX_PATH];

	safe_snprintf(filename, _MAX_PATH, "%s.ent", name);

	{
		FILE* f = SafeOpenRead(filename);
		Log("\nReading %s.\n", filename); // Added by Nem.

		g_entdatasize = q_filelength(f);

		assume(g_entdatasize != 0, "No entity data.");
		assume(
			g_entdatasize < sizeof(g_dentdata),
			"Entity data size exceedes dentdata limit."
		);

		SafeRead(f, g_dentdata.data(), g_entdatasize);

		fclose(f);

		if (g_dentdata[g_entdatasize - 1] != u8'\0') {
			//            Log("g_dentdata[g_entdatasize-1] = %d\n",
			//            g_dentdata[g_entdatasize-1]);

			if (g_parse) // Added by Nem.
			{
				ParseEntityData("", 0, "\n", 1, "\0", 1);
			} else if (g_dentdata[g_entdatasize - 1] != u8'\0') {
				g_dentdata[g_entdatasize] = u8'\0';
				g_entdatasize++;
			}
		}
	}
}

//======================================================================

static void Usage(void) {
	Banner();
	Log("\n-= %s Options =-\n\n", g_Program);

	Log("    -export         : Export entity data\n");
	Log("    -import         : Import entity data\n\n");

	Log("    -parse          : Parse and format entity data\n\n");
	Log("    -textureexport  : Export texture data\n");
	Log("    -textureimport  : Import texture data\n");
	Log("    -textureparse   : Parse and format texture data\n\n");
	Log("    -writeextentfile : Create extent file for the map\n");
	Log("    -deleteembeddedlightmaps : Delete textures created by hlrad\n"
	);

	Log("    -texdata #      : Alter maximum texture memory limit (in kb)\n"
	);
	Log("    -chart          : Display bsp statitics\n");
	Log("    -noinfo         : Do not show tool configuration information\n\n"
	);

	Log("    mapfile         : The mapfile to process\n\n");

	exit(1);
}

// =====================================================================================
//  Settings
// =====================================================================================
static void Settings() {
	char const * tmp;

	if (!g_info) {
		return;
	}

	Log("\n-= Current %s Settings =-\n", g_Program);
	Log("Name               |  Setting  |  Default\n"
		"-------------------|-----------|-------------------------\n");

	// ZHLT Common Settings
	Log("chart               [ %7s ] [ %7s ]\n",
		g_chart ? "on" : "off",
		cli_option_defaults::chart ? "on" : "off");
	Log("max texture memory  [ %7td ] [ %7td ]\n",
		g_max_map_miptex,
		cli_option_defaults::max_map_miptex);

	switch (g_mode) {
		case hl_import:
			tmp = "Import";
			break;
		case hl_export:
			tmp = "Export";
			break;
		case hl_undefined:
		default:
			tmp = "N/A";
			break;
	}

	Log("\n");

	// RipEnt Specific Settings
	Log("mode                [ %7s ] [ %7s ]\n", tmp, "N/A");
	Log("parse               [ %7s ] [ %7s ]\n",
		g_parse ? "on" : "off",
		DEFAULT_PARSE ? "on" : "off");
	switch (g_texturemode) {
		case hl_import:
			tmp = "Import";
			break;
		case hl_export:
			tmp = "Export";
			break;
		case hl_undefined:
		default:
			tmp = "N/A";
			break;
	}
	Log("texture mode        [ %7s ] [ %7s ]\n", tmp, "N/A");
	Log("texture parse       [ %7s ] [ %7s ]\n",
		g_textureparse ? "on" : "off",
		DEFAULT_TEXTUREPARSE ? "on" : "off");
	Log("write extent file   [ %7s ] [ %7s ]\n",
		g_writeextentfile ? "on" : "off",
		DEFAULT_WRITEEXTENTFILE ? "on" : "off");
	Log("delete rad textures [ %7s ] [ %7s ]\n",
		g_deleteembeddedlightmaps ? "on" : "off",
		DEFAULT_DELETEEMBEDDEDLIGHTMAPS ? "on" : "off");

	Log("\n\n");
}

/*
 * ============
 * main
 * ============
 */
int main(int argc, char** argv) {
	g_Program = "RIPENT";

	int argcold = argc;
	char** argvold = argv;
	{
		int argc;
		char** argv;
		ParseParamFile(argcold, argvold, argc, argv);
		{
			if (argc == 1) {
				Usage();
			}

			for (std::size_t i = 1; i < argc; i++) {
				if (strings_equal_with_ascii_case_insensitivity(
						argv[i], u8"-import"
					)) {
					g_mode = hl_import;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-export"
						   )) {
					g_mode = hl_export;
				}
				// g_parse: command line switch (-parse).
				// Added by: Ryan Gregg aka Nem
				else if (strings_equal_with_ascii_case_insensitivity(
							 argv[i], u8"-parse"
						 )) {
					g_parse = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-texdata"
						   )) {
					if (i + 1 < argc) // added "1" .--vluzacn
					{
						int x = atoi(argv[++i]) * 1024;

						// if (x > g_max_map_miptex) //--vluzacn
						{ g_max_map_miptex = x; }
					} else {
						Usage();
					}
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-chart"
						   )) {
					g_chart = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-noinfo"
						   )) {
					g_info = false;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-textureimport"
						   )) {
					g_texturemode = hl_import;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-textureexport"
						   )) {
					g_texturemode = hl_export;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-textureparse"
						   )) {
					g_textureparse = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-writeextentfile"
						   )) {
					g_writeextentfile = true;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-deleteembeddedlightmaps"
						   )) {
					g_deleteembeddedlightmaps = true;
				} else if (argv[i][0] == '-') //--vluzacn
				{
					Log("Unknown option: '%s'\n", argv[i]);
					Usage();
				} else {
					safe_strncpy(g_Mapname, argv[i], _MAX_PATH);
					FlipSlashes(g_Mapname);
					StripExtension(g_Mapname);
				}
			}

			std::filesystem::path source;
			source = g_Mapname;
			source += u8".bsp";

			if (!std::filesystem::exists(source)) {
				Log("bspfile '%s' does not exist\n",
					source.c_str()); //--vluzacn
				Usage();
			}

			LogStart(argcold, argvold);
			log_arguments(argc, argv);
			atexit(LogEnd);

			Settings();

			dtexdata_init();
			atexit(dtexdata_free);

			// BEGIN RipEnt
			time_counter timeCounter;

			ReadBSP(g_Mapname);
			bool updatebsp = false;
			if (g_deleteembeddedlightmaps) {
				DeleteEmbeddedLightmaps();
				updatebsp = true;
			}
			switch (g_mode) {
				case hl_import:
					ReadEntities(g_Mapname);
					updatebsp = true;
					break;
				case hl_export:
					WriteEntities(g_Mapname);
					break;
				case hl_undefined:
					break;
			}
			switch (g_texturemode) {
				case hl_import:
					ReadTextures(g_Mapname);
					updatebsp = true;
					break;
				case hl_export:
					WriteTextures(g_Mapname);
					break;
				case hl_undefined:
					break;
			}
			if (g_chart) {
				print_bsp_file_sizes(bspGlobals);
			}
			if (updatebsp) {
				WriteBSP(g_Mapname);
			}

			LogTimeElapsed(timeCounter.get_total());
			// END RipEnt
		}
	}

	return 0;
}

// do nothing - we don't have params to fetch
void GetParamsFromEnt(entity_t* mapent) { }
