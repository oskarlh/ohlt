

#include "cmdlib.h"

#include "hlassert.h"
#include "log.h"
#include "mathlib.h"
#include "messages.h"
#include "utf8.h"

#include <bit>
#include <cstring>
#include <ranges>

inline void getFilePositions(
	char const * path, int* extension_position, int* directory_position
) {
	char const * ptr = strrchr(path, '.');
	if (ptr == 0) {
		*extension_position = -1;
	} else {
		*extension_position = ptr - path;
	}

	ptr = std::max(strrchr(path, '/'), strrchr(path, '\\'));
	if (ptr == 0) {
		*directory_position = -1;
	} else {
		*directory_position = ptr - path;
		if (*directory_position > *extension_position) {
			*extension_position = -1;
		}

		// cover the case where we were passed a directory - get 2nd-to-last
		// slash
		if (*directory_position == (int) strlen(path) - 1) {
			do {
				--(*directory_position);
			} while (*directory_position > -1
					 && path[*directory_position] != '/'
					 && path[*directory_position] != '\\');
		}
	}
}

void DefaultExtension(char* path, char const * extension) {
	int extension_pos, directory_pos;
	getFilePositions(path, &extension_pos, &directory_pos);
	if (extension_pos == -1) {
		strcat(path, extension);
	}
}

void ExtractFilePath(char const * const path, char* dest) {
	int extension_pos, directory_pos;
	getFilePositions(path, &extension_pos, &directory_pos);
	if (directory_pos != -1) {
		memcpy(dest, path, directory_pos + 1); // include directory slash
		dest[directory_pos + 1] = 0;
	} else {
		dest[0] = 0;
	}
}

void ExtractFile(char const * const path, char* dest) {
	int extension_pos, directory_pos;
	getFilePositions(path, &extension_pos, &directory_pos);

	int length = strlen(path);

	length -= directory_pos + 1;

	memcpy(
		dest, path + directory_pos + 1, length
	); // exclude directory slash
	dest[length] = 0;
}

//-------------------------------------------------------------------

//=============================================================================

bool FORMAT_PRINTF(3, 4) safe_snprintf(
	char* const dest, size_t const count, char const * const args, ...
) {
	size_t amt;
	va_list argptr;

	hlassert(count > 0);

	va_start(argptr, args);
	amt = vsnprintf(dest, count, args, argptr);
	va_end(argptr);

	// truncated (bad!, snprintf doesn't null terminate the string when this
	// happens)
	if (amt == count) {
		dest[count - 1] = 0;
		return false;
	}

	return true;
}

bool safe_strncpy(
	char* const dest, char const * const src, size_t const count
) {
	return safe_snprintf(dest, count, "%s", src);
}

bool safe_strncat(
	char* const dest, char const * const src, size_t const count
) {
	if (count) {
		strncat(dest, src, count);

		dest[count - 1] = 0; // Ensure it is null terminated
		return true;
	} else {
		Warning("safe_strncat passed empty count");
		return false;
	}
}
