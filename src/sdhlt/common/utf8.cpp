#include "utf8.h"

#include <bit>

namespace utf8_internal {
	bool validate_utf8_continuing_code_units(const unsigned char* & it, const unsigned char* end, unsigned char firstCodeUnitInCodePoint) noexcept {
		std::size_t codeUnitsToCheck = std::countl_one(firstCodeUnitInCodePoint) - 1;
		const std::size_t unitsRemaining = end - it;

		bool invalid = codeUnitsToCheck == 0 || codeUnitsToCheck > 3;
		bool earlyEnd = unitsRemaining < codeUnitsToCheck;
		if(invalid || earlyEnd) [[unlikely]] {
			return false;
		}
		do {
			const unsigned char continuingCodeUnit = *it;
			if(continuingCodeUnit < 0b1000'0000 || continuingCodeUnit > 0b1011'1111) [[unlikely]] {
				return false;
			}
			++it;
		} while(--codeUnitsToCheck);
		return true;
	}
}
