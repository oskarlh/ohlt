#include "csg.h"
#include "parsing.h"
#include "utf8.h"

#include <expected>
#include <string>
using namespace std::literals;

// TODO: Use these with the tools/mods/<modname>/nowadtextures_blocklist.cfg
// files

std::vector<std::u8string>
parse_nowadtextures_blocklist(std::u8string_view fileContents) {
	std::u8string_view remaining{ fileContents };
	std::vector<std::u8string> wads;

	while (true) {
		std::u8string_view const word = next_word(remaining);
		if (word.empty()) {
			return std::move(wads);
		}
		wads.emplace_back(word);
	}
}

std::optional<std::vector<std::u8string>>
load_nowadtextures_blocklist(std::filesystem::path wadConfigPath) {
	Log("Loading %s\n", wadConfigPath.c_str());
	std::optional<std::u8string> maybeContent = read_utf8_file(
		wadConfigPath, true
	);
	if (!maybeContent) {
		Warning("Failed to read the WAD config");
		return std::nullopt;
	}
	return parse_nowadtextures_blocklist(maybeContent.value());
}
