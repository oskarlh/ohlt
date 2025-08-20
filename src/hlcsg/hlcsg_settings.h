#pragma once

#include "legacy_character_encodings.h"

struct hlcsg_settings final {
	legacy_encoding legacyMapEncoding{ legacy_encoding::windows_1252 };
	bool forceLegacyMapEncoding{ false };

	// Note: The old -scale used -1.0
	// to mean "disable scaling". Now for
	// -mapScale we use 1.0 instead
	double mapScale{ 1 };

	double tinyTreshold = 0;
};
