#include <array>
#include <string>

struct wadinfo_t
{
    std::array<char8_t, 4> identification; // should be WAD2/WAD3
    std::int32_t numlumps;
    std::int32_t infotableofs;
};

constexpr bool has_wad_identification(const wadinfo_t& wadHeader) {
	using namespace std::literals;
	
	std::u8string_view id{wadHeader.identification};
    return id == u8"WAD2"sv || id == u8"WAD3"sv;
}

constexpr std::size_t MAXWADNAME = 16;
struct lumpinfo_t // Lump info in WAD
{
    std::int32_t filepos;
    std::int32_t disksize;
    std::int32_t size; // Uncompressed
    std::uint8_t type;
    std::uint8_t compression;
    std::uint8_t pad1, pad2;
    // Must be null terminated
    // Upper case
    std::array<char8_t, MAXWADNAME> name{};
};
