#pragma once

#include "legacy_character_encodings.h"

struct hlcsg_settings final {
	legacy_encoding legacyMapEncoding{ legacy_encoding::windows_1252 };
	bool forceLegacyMapEncoding{ false };
};
