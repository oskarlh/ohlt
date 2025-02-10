#pragma once

#include "internal_types/internal_types.h"

struct csg_entity final {
	entity_t ent;
	brush_count firstBrush;
	entity_local_brush_count numBrushes;

	//	std::span<csg_brush> brushes(std::span<csg_brush> globalContainer
	//	) const noexcept {
	//		return globalContainer.subspan(firstBrush, numBrushes);
	//	}
};
