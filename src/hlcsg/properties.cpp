// KGP -- added in for use with HLCSG_NULLIFY_INVISIBLE

#include "csg.h"

#include <fstream>
#include <istream>

std::set<std::u8string> g_invisible_items;

void properties_initialize(char const * filename) {
	if (filename == nullptr) {
		return;
	}

	if (std::filesystem::exists(filename)) {
		Log("Loading null entity list from '%s'\n", filename);
	} else {
		Error("Could not find null entity list file '%s'\n", filename);
		return;
	}

	std::ifstream file(filename, std::ios::in);
	if (!file) {
		file.close();
		return;
	}

	// begin reading list of items
	char line[MAX_VAL]; // MAX_VALUE //vluzacn
	std::memset(line, 0, sizeof(char) * 4096);
	while (!file.eof()) {
		std::string str;
		std::getline(file, str);
		{ //--vluzacn
			char* s = c_strdup(str.c_str());
			int i;
			for (i = 0; s[i] != '\0'; i++) {
				if (s[i] == '\n' || s[i] == '\r') {
					s[i] = '\0';
				}
			}
			str.assign(s);
			free(s);
		}
		if (str.size() < 1) {
			continue;
		}
		g_invisible_items.insert(std::u8string(
			(char8_t const *) str.data(),
			(char8_t const *) str.data() + str.length()
		));
	}
	file.close();
}
