//
//	Transparency Arrays for sparse and vismatrix methods
//

#include "hlrad.h"

#include <limits>

struct transList_t final {
	unsigned p1;
	unsigned p2;
	unsigned data_index;
};

static transList_t* s_raw_list = nullptr;
static unsigned int s_raw_count = 0;
static unsigned int s_max_raw_count
	= 0; // Current array maximum (used for reallocs)

static transList_t* s_sorted_list = nullptr; // Sorted first by p1 then p2
static unsigned int s_sorted_count = 0;

//===============================================
// AddTransparencyToRawArray
//===============================================
static std::size_t AddTransparencyToDataList(
	float3_array const & trans, std::vector<float3_array>& transparencyList
) {
	// Check if this value is in list already
	for (std::size_t i = 0; i < transparencyList.size(); ++i) {
		if (vectors_almost_same(trans, transparencyList[i])) {
			return i;
		}
	}

	std::size_t index = transparencyList.size();
	transparencyList.emplace_back(trans);

	return index;
}

//===============================================
// AddTransparencyToRawArray
//===============================================
void AddTransparencyToRawArray(
	unsigned const p1,
	unsigned const p2,
	float3_array const & trans,
	std::vector<float3_array>& transparencyList
) {
	// Make thread safe
	ThreadLock();

	std::size_t data_index = AddTransparencyToDataList(
		trans, transparencyList
	);

	// Realloc if needed
	while (s_raw_count >= s_max_raw_count) {
		std::uint32_t old_max_count = s_max_raw_count;
		s_max_raw_count = std::max(
			64u, (std::uint32_t)((double) s_max_raw_count * 1.41)
		);
		if (s_max_raw_count >= std::numeric_limits<std::int32_t>::digits) {
			Error("AddTransparencyToRawArray: array size exceeded INT_MAX");
		}

		s_raw_list = (transList_t*) realloc(
			s_raw_list, sizeof(transList_t) * s_max_raw_count
		);

		hlassume(s_raw_list != nullptr, assume_NoMemory);

		std::fill_n(
			&s_raw_list[old_max_count],
			s_max_raw_count - old_max_count,
			transList_t{}
		);
	}

	s_raw_list[s_raw_count].p1 = p1;
	s_raw_list[s_raw_count].p2 = p2;
	s_raw_list[s_raw_count].data_index = data_index;

	s_raw_count++;

	// unlock list
	ThreadUnlock();
}

//===============================================
// SortList
//===============================================
static int SortList(void const * a, void const * b) {
	transList_t const * item1 = (transList_t*) a;
	transList_t const * item2 = (transList_t*) b;

	if (item1->p1 == item2->p1) {
		return item1->p2 - item2->p2;
	}
	return item1->p1 - item2->p1;
}

//===============================================
// CreateFinalTransparencyArrays
//===============================================
void CreateFinalTransparencyArrays(
	char const * print_name, std::vector<float3_array>& transparencyList
) {
	if (s_raw_count == 0) {
		s_raw_list = nullptr;
		s_raw_count = s_max_raw_count = 0;
		return;
	}

	// double sized (faster find function for sorted list)
	s_sorted_count = s_raw_count * 2;
	s_sorted_list = (transList_t*) malloc(
		sizeof(transList_t) * s_sorted_count
	);

	hlassume(s_sorted_list != nullptr, assume_NoMemory);

	// First half have p1>p2
	for (unsigned int i = 0; i < s_raw_count; i++) {
		s_sorted_list[i].p1 = s_raw_list[i].p2;
		s_sorted_list[i].p2 = s_raw_list[i].p1;
		s_sorted_list[i].data_index = s_raw_list[i].data_index;
	}
	// Second half have p1<p2
	memcpy(
		&s_sorted_list[s_raw_count],
		s_raw_list,
		sizeof(transList_t) * s_raw_count
	);

	// free old array
	free(s_raw_list);
	s_raw_list = nullptr;
	s_raw_count = s_max_raw_count = 0;

	// need to sorted for fast search function
	qsort(s_sorted_list, s_sorted_count, sizeof(transList_t), SortList);

	size_t size = s_sorted_count * sizeof(transList_t)
		+ transparencyList.size() * sizeof(float3_array);
	if (size > 1024 * 1024) {
		Log("%-20s: %5.1f megs \n",
			print_name,
			(double) size / (1024.0 * 1024.0));
	} else if (size > 1024) {
		Log("%-20s: %5.1f kilos\n", print_name, (double) size / 1024.0);
	} else {
		Log("%-20s: %5.1f bytes\n", print_name, (double) size); //--vluzacn
	}
	Developer(
		developer_level::message,
		"\ts_trans_count=%zu\ts_sorted_count=%d\n",
		transparencyList.size(),
		s_sorted_count
	); //--vluzacn
}

//===============================================
// FreeTransparencyArrays
//===============================================
void FreeTransparencyArrays() {
	if (s_sorted_list) {
		free(s_sorted_list);
	}

	s_sorted_list = nullptr;

	s_sorted_count = 0;
}

//===============================================
// GetTransparency -- find transparency from list. remembers last location
//===============================================
void GetTransparency(
	unsigned const p1,
	unsigned const p2,
	float3_array& trans,
	unsigned int& next_index,
	std::vector<float3_array> const & transparencyList
) {
	trans.fill(1.0);

	for (unsigned i = next_index; i < s_sorted_count; i++) {
		if (s_sorted_list[i].p1 < p1) {
			continue;
		} else if (s_sorted_list[i].p1 == p1) {
			if (s_sorted_list[i].p2 < p2) {
				continue;
			} else if (s_sorted_list[i].p2 == p2) {
				trans = transparencyList[s_sorted_list[i].data_index];
				next_index = i + 1;

				return;
			} else // if ( s_sorted_list[i].p2 > p2 )
			{
				next_index = i;

				return;
			}
		} else // if ( s_sorted_list[i].p1 > p1 )
		{
			next_index = i;

			return;
		}
	}

	next_index = s_sorted_count;
}

struct styleList_t final {
	unsigned p1;
	unsigned p2;
	char style;
};

static styleList_t* s_style_list = nullptr;
static std::uint32_t s_style_count = 0;
static std::uint32_t s_max_style_count = 0;

void AddStyleToStyleArray(
	unsigned const p1, unsigned const p2, int const style
) {
	if (style == -1) {
		return;
	}
	// make thread safe
	ThreadLock();

	// realloc if needed
	while (s_style_count >= s_max_style_count) {
		std::uint32_t old_max_count = s_max_style_count;
		s_max_style_count = std::max(
			64u, (std::uint32_t)((double) s_max_style_count * 1.41)
		);
		if (s_max_style_count
			>= (std::uint32_t) std::numeric_limits<std::int32_t>::digits) {
			Error("AddStyleToStyleArray: array size exceeded INT_MAX");
		}

		s_style_list = (styleList_t*) realloc(
			s_style_list, sizeof(styleList_t) * s_max_style_count
		);

		hlassume(s_style_list != nullptr, assume_NoMemory);

		std::fill_n(
			&s_style_list[old_max_count],
			(s_max_style_count - old_max_count),
			styleList_t{}
		);
	}

	s_style_list[s_style_count].p1 = p1;
	s_style_list[s_style_count].p2 = p2;
	s_style_list[s_style_count].style = (char) style;

	s_style_count++;

	// unlock list
	ThreadUnlock();
}

static int SortStyleList(void const * a, void const * b) {
	styleList_t const * item1 = (styleList_t*) a;
	styleList_t const * item2 = (styleList_t*) b;

	if (item1->p1 == item2->p1) {
		return item1->p2 - item2->p2;
	} else {
		return item1->p1 - item2->p1;
	}
}

void CreateFinalStyleArrays(char const * print_name) {
	if (s_style_count == 0) {
		return;
	}
	// need to sorted for fast search function
	qsort(s_style_list, s_style_count, sizeof(styleList_t), SortStyleList);

	size_t size = s_max_style_count * sizeof(styleList_t);
	if (size > 1024 * 1024) {
		Log("%-20s: %5.1f megs \n",
			print_name,
			(double) size / (1024.0 * 1024.0));
	} else if (size > 1024) {
		Log("%-20s: %5.1f kilos\n", print_name, (double) size / 1024.0);
	} else {
		Log("%-20s: %5.1f bytes\n", print_name, (double) size); //--vluzacn
	}
}

void FreeStyleArrays() {
	if (s_style_count) {
		free(s_style_list);
	}

	s_style_list = nullptr;

	s_max_style_count = s_style_count = 0;
}

void GetStyle(
	unsigned const p1,
	unsigned const p2,
	int& style,
	unsigned int& next_index
) {
	style = -1;

	for (unsigned i = next_index; i < s_style_count; i++) {
		if (s_style_list[i].p1 < p1) {
			continue;
		} else if (s_style_list[i].p1 == p1) {
			if (s_style_list[i].p2 < p2) {
				continue;
			} else if (s_style_list[i].p2 == p2) {
				style = (int) s_style_list[i].style;
				next_index = i + 1;

				return;
			} else // if ( s_style_list[i].p2 > p2 )
			{
				next_index = i;

				return;
			}
		} else // if ( s_style_list[i].p1 > p1 )
		{
			next_index = i;

			return;
		}
	}

	next_index = s_style_count;
}
