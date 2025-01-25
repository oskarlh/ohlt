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

	// Begin reading list of items
	std::string str;
	while (!file.eof()) {
		str.clear();
		std::getline(file, str);
		for (int i = 0; i != str.length(); i++) {
			if (str[i] == '\n' || str[i] == '\r') {
				str.resize(i);
				break;
			}
		}
		if (str.empty()) {
			continue;
		}
		g_invisible_items.insert(
			std::u8string((char8_t const *) str.data(), str.size())
		);
	}
	file.close();
}
