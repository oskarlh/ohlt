#include "bsp_file_sizes.h"
#include "bspfile.h"
#include "cli_option_defaults.h"
#include "cmdlib.h"
#include "cmdlinecfg.h"
#include "filelib.h"
#include "hlassert.h"
#include "log.h"
#include "messages.h"
#include "ripent_cli_option_defaults.h"
#include "time_counter.h"
#include "vector_for_overwriting.h"
#include "wad_structs.h"
#include "wad_texture_name.h"
#include "win32fix.h"

#include <cstring>
#include <filesystem>

enum class hl_types {
	hl_undefined,
	hl_export,
	hl_import
};

static hl_types g_mode = hl_types::hl_undefined;
static hl_types g_texturemode = hl_types::hl_undefined;

static bool g_chart = cli_option_defaults::chart;
static bool g_info = cli_option_defaults::info;
static bool g_parse = ripent_cli_option_defaults::parse;
static bool g_textureparse = ripent_cli_option_defaults::textureParse;

static bool g_writeextentfile = ripent_cli_option_defaults::writeExtentFile;

static bool g_deleteembeddedlightmaps
	= ripent_cli_option_defaults::deleteEmbeddedLightMaps;

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
	int iLine = 0;  // Current line in g_dentdata.

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
		hlassume(CalcFaceExtents_test(), assume_msg::first);
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
	filename += std::filesystem::path{
		u8".bsp", std::filesystem::path::generic_format
	};

	Log("\nUpdating %s.\n", filename.c_str());
	WriteBSPFile(filename);
}

static void WriteTextures(char const * const name) {
	char wadfilename[_MAX_PATH];
	FILE* wadfile;
	safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
	std::filesystem::remove(wadfilename);
	wadfile = SafeOpenWrite(wadfilename);
	Log("Writing %s.\n", wadfilename);

	char texfilename[_MAX_PATH];
	safe_snprintf(texfilename, _MAX_PATH, "%s.tex", name);
	std::filesystem::remove(texfilename);

	int dataofs = (int) (intptr_t)
		& ((dmiptexlump_t*) NULL)
			  ->dataofs[((dmiptexlump_t*) g_dtexdata.data())->nummiptex];
	int wadofs = sizeof(wadinfo_t);

	wadinfo_t header{};
	header.identification[0] = 'W';
	header.identification[1] = 'A';
	header.identification[2] = 'D';
	header.identification[3] = '3';
	std::uint32_t const numMipTex
		= ((dmiptexlump_t*) g_dtexdata.data())->nummiptex;
	header.infotableofs = g_texdatasize - dataofs + wadofs;

	std::vector<wad_lumpinfo> info;
	info.reserve(numMipTex);

	struct texture_name_and_dimensions { };

	std::vector<miptex_t> includedTextures;
	std::vector<miptex_t> externalTextures;
	includedTextures.reserve(numMipTex);
	externalTextures.reserve(numMipTex);

	for (int i = 0; i < numMipTex; i++) {
		std::uint32_t const ofs
			= ((dmiptexlump_t const *) g_dtexdata.data())->dataofs[i];

		miptex_t tex = *((miptex_t const *) (g_dtexdata.data() + ofs));
		bool included = tex.offsets[0] > 0;

		if (!tex.name.validate_and_normalize()) {
			Error("Invalid texture name for texture #%i\n", i);
		}

		if (!included) {
			externalTextures.emplace_back(tex);
			continue;
		}

		includedTextures.emplace_back(tex);

		std::uint32_t size = g_texdatasize - ofs;
		for (int j = 0; j < numMipTex; ++j) {
			if (ofs < ((dmiptexlump_t*) g_dtexdata.data())->dataofs[j]
			    && ofs + size
			        > ((dmiptexlump_t*) g_dtexdata.data())->dataofs[j]) {
				size = ((dmiptexlump_t*) g_dtexdata.data())->dataofs[j]
					- ofs;
			}
		}

		wad_lumpinfo& lumpinf{ info.emplace_back() };
		lumpinf.filepos = ofs - dataofs + wadofs;
		lumpinf.disksize = size;
		lumpinf.size = size;
		lumpinf.type = 67;
		lumpinf.compression = 0;
		lumpinf.name = tex.name;
	}

	header.numlumps = info.size();

	SafeWrite(wadfile, &header, wadofs);
	SafeWrite(
		wadfile,
		(byte*) g_dtexdata.data() + dataofs,
		g_texdatasize - dataofs
	);
	SafeWrite(wadfile, info.data(), info.size() * sizeof(wad_lumpinfo));

	fclose(wadfile);

	if (!g_textureparse) {
		return;
	}
	FILE* texfile = SafeOpenWrite(texfilename);
	Log("Writing %s.\n", texfilename);
	fprintf(
		texfile,
		"%zu textures included in the .bsp and exported to the .wad file\n",
		includedTextures.size()
	);
	for (miptex_t const & texture : includedTextures) {
		fprintf(
			texfile,
			"%s %d %d\n",
			texture.name.c_str(),
			texture.width,
			texture.height
		);
	}

	fprintf(
		texfile,
		"\n%zu textures used by the .bsp but not included in the .bsp itself - the game loads them from .wad files\n",
		externalTextures.size()
	);
	for (miptex_t const & texture : externalTextures) {
		fprintf(
			texfile,
			"%s %d %d\n",
			texture.name.c_str(),
			texture.width,
			texture.height
		);
	}

	fclose(texfile);
}

inline void skipspace(FILE* f) {
	fscanf(f, "%*[ \t\r\n]s");
}

inline void skipline(FILE* f) {
	fscanf(f, "%*[^\r\n]s");
}

// TODO: Rewrite this so -textureimport only replaces textures.
// (Except for animated/tiled textures, for those we might need to add and
// remove textures based on the new number of frames/tiles).
// Any textures not in the .wad should be made external
static void ReadTextures(char const * name) {
	char wadfilename[_MAX_PATH];
	FILE* wadfile;
	safe_snprintf(wadfilename, _MAX_PATH, "%s.wad", name);
	wadfile = SafeOpenRead(wadfilename);
	Log("Reading %s.\n", wadfilename);

	wadinfo_t header;
	int wadofs = sizeof(wadinfo_t);
	SafeRead(wadfile, &header, wadofs);
	((dmiptexlump_t*) g_dtexdata.data())->nummiptex = header.numlumps;
	int dataofs = (int) (intptr_t)
		& ((dmiptexlump_t*) NULL)
			  ->dataofs[((dmiptexlump_t*) g_dtexdata.data())->nummiptex];
	g_texdatasize = header.infotableofs - wadofs + dataofs;

	SafeRead(
		wadfile,
		(byte*) g_dtexdata.data() + dataofs,
		g_texdatasize - dataofs
	);

	std::vector<wad_lumpinfo> info;
	info.resize(header.numlumps, {});
	SafeRead(wadfile, info.data(), header.numlumps * sizeof(wad_lumpinfo));

	for (int i = 0; i < header.numlumps; i++) {
		((dmiptexlump_t*) g_dtexdata.data())->dataofs[i] = info[i].filepos
			- wadofs + dataofs;
	}
	fclose(wadfile);
}

static void WriteEntities(char const * const name) {
	std::filesystem::path filePath{ name };
	filePath += u8".ent";

	std::filesystem::remove(filePath);

	vector_for_overwriting<char> bak_dentdata;

	if (g_parse) {
		bak_dentdata.reset(g_entdatasize);
		std::memcpy(bak_dentdata.data(), g_dentdata.data(), g_entdatasize);
		ParseEntityData("  ", 2, "\r\n", 2, "", 0);
	}

	FILE* f = SafeOpenWrite(filePath.c_str());
	Log("\nWriting %s.\n", filePath.c_str());
	SafeWrite(f, g_dentdata.data(), g_entdatasize);
	fclose(f);
	if (g_parse) {
		g_entdatasize = bak_dentdata.size();
		std::memcpy(
			g_dentdata.data(), bak_dentdata.data(), bak_dentdata.size()
		);
	}
}

static void ReadEntities(char const * const name) {
	char filename[_MAX_PATH];

	safe_snprintf(filename, _MAX_PATH, "%s.ent", name);

	{
		FILE* f = SafeOpenRead(filename);
		Log("\nReading %s.\n", filename);

		g_entdatasize = q_filelength(f);

		assume(g_entdatasize != 0, "No entity data.");
		assume(
			g_entdatasize < sizeof(g_dentdata),
			"Entity data size exceedes dentdata limit."
		);

		SafeRead(f, g_dentdata.data(), g_entdatasize);

		fclose(f);

		if (g_dentdata[g_entdatasize - 1] != u8'\0') {
			if (g_parse) {
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
	Log("\n-= %s Options =-\n\n", (char const *) g_Program.data());

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

	Log("\n-= Current %s Settings =-\n", (char const *) g_Program.data());
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
		case hl_types::hl_import:
			tmp = "Import";
			break;
		case hl_types::hl_export:
			tmp = "Export";
			break;
		case hl_types::hl_undefined:
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
		case hl_types::hl_import:
			tmp = "Import";
			break;
		case hl_types::hl_export:
			tmp = "Export";
			break;
		case hl_types::hl_undefined:
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
	g_Program = u8"RIPENT";

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
					g_mode = hl_types::hl_import;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-export"
						   )) {
					g_mode = hl_types::hl_export;
				}
				// g_parse: command line switch (-parse).
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
					g_texturemode = hl_types::hl_import;
				} else if (strings_equal_with_ascii_case_insensitivity(
							   argv[i], u8"-textureexport"
						   )) {
					g_texturemode = hl_types::hl_export;
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
				} else if (argv[i][0] == '-') {
					Log("Unknown option: '%s'\n", argv[i]);
					Usage();
				} else {
					g_Mapname = std::filesystem::path(
						argv[i], std::filesystem::path::auto_format
					);
					g_Mapname.replace_extension(std::filesystem::path{});
				}
			}

			std::filesystem::path source{
				path_to_temp_file_with_extension(g_Mapname, u8".bsp")
			};

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

			// BEGIN RipEnt
			time_counter timeCounter;

			ReadBSP(g_Mapname.c_str());
			bool updatebsp = false;
			if (g_deleteembeddedlightmaps) {
				DeleteEmbeddedLightmaps();
				updatebsp = true;
			}
			switch (g_mode) {
				case hl_types::hl_import:
					ReadEntities(g_Mapname.c_str());
					updatebsp = true;
					break;
				case hl_types::hl_export:
					WriteEntities(g_Mapname.c_str());
					break;
				case hl_types::hl_undefined:
					break;
			}
			switch (g_texturemode) {
				case hl_types::hl_import:
					ReadTextures(g_Mapname.c_str());
					updatebsp = true;
					break;
				case hl_types::hl_export:
					WriteTextures(g_Mapname.c_str());
					break;
				case hl_types::hl_undefined:
					break;
			}
			if (g_chart) {
				print_bsp_file_sizes(bspGlobals);
			}
			if (updatebsp) {
				WriteBSP(g_Mapname.c_str());
			}

			LogTimeElapsed(timeCounter.get_total());
		}
	}

	return 0;
}

// do nothing - we don't have params to fetch
void GetParamsFromEnt(entity_t* mapent) { }
