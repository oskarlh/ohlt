#include "hlcsg.h"
#include "log.h"

#include <fstream>

std::set<std::u8string> g_invisible_items;

void properties_initialize(std::filesystem::path filePath) {
	if (filePath.empty()) {
		return;
	}

	if (std::filesystem::exists(filePath)) {
		Log("Loading null entity list from '%s'\n", filePath.c_str());
	} else {
		Error(
			"Could not find null entity list file '%s'\n", filePath.c_str()
		);
		return;
	}

	std::ifstream file(filePath.c_str(), std::ios::in);
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
