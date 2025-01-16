#pragma once

#include "cmdlib.h"
#include "legacy_character_encodings.h"

#define MAXTOKEN 4096

extern std::u8string g_token;

extern void LoadScriptFile(
	char const * const filename, legacy_encoding legacyEncoding,
	bool forceLegacyEncoding
);
extern void ParseFromMemory(std::u8string_view buffer);

extern bool GetToken(bool crossline);
extern void UnGetToken();
extern bool TokenAvailable();

#define MAX_WAD_PATHS 42
extern char g_szWadPaths[MAX_WAD_PATHS][_MAX_PATH];
