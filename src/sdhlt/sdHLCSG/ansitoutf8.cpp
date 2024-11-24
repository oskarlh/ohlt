
#include "csg.h"

#ifdef HLCSG_GAMETEXTMESSAGE_UTF8
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

char * ANSItoUTF8 (std::string_view ansiString)
{
	const int utf16Length = MultiByteToWideChar (CP_ACP, 0, ansiString.data(), ansiString.size(), nullptr, 0);
	std::wstring utf16;
	utf16.resize_and_overwrite(utf16Length, [](wchar_t* buffer, std::size_t buffer_size) noexcept {
		return MultiByteToWideChar(CP_ACP, 0, ansiString.data(), ansiString.size(), buffer, (int) buffer_size);
	});

	int utf8Length = WideCharToMultiByte (CP_UTF8, 0, utf16.data(), utf16.size(), nullptr, 0, nullptr, nullptr);
	std::u8string utf8;
	utf8.resize_and_overwrite(utf8Length, [](uchar8_t* buffer, std::size_t buffer_size) noexcept {
		WideCharToMultiByte (CP_UTF8, 0, utf16.data(), utf16.size(), buffer, (int) buffer_size, nullptr, nullptr);
	});
	return utf8;
}
#endif
