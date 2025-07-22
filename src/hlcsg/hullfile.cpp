#include "csg.h"
#include "hull_size.h"

void LoadHullfile(char const * filename) {
	if (filename == nullptr) {
		return;
	}

	if (std::filesystem::exists(filename)) {
		Log("Loading hull definitions from '%s'\n", filename);
	} else {
		Error("Could not find hull definition file '%s'\n", filename);
		return;
	}

	FILE* file = fopen(filename, "r");

	char magic = (char) fgetc(file);
	rewind(file);

	if (magic == '(') { // Test for old-style hull-file

		for (int i = 0; i < NUM_HULLS; i++) {
			float x1, y1, z1;
			float x2, y2, z2;
			int count = fscanf(
				file,
				"( %f %f %f ) ( %f %f %f )\n",
				&x1,
				&y1,
				&z1,
				&x2,
				&y2,
				&z2
			);
			if (count != 6) {
				Error(
					"Could not parse old hull definition file '%s' (%d, %d)\n",
					filename,
					i,
					count
				);
			}

			g_hull_size[i][0][0] = x1;
			g_hull_size[i][0][1] = y1;
			g_hull_size[i][0][2] = z1;

			g_hull_size[i][1][0] = x2;
			g_hull_size[i][1][1] = y2;
			g_hull_size[i][1][2] = z2;
		}

	} else {
		// Skip hull 0 (visibile polygons)
		for (int i = 1; i < NUM_HULLS; i++) {
			float x1, y1, z1;
			int count = fscanf(file, "%f %f %f\n", &x1, &y1, &z1);
			if (count != 3) {
				Error(
					"Could not parse new hull definition file '%s' (%d, %d)\n",
					filename,
					i,
					count
				);
			}
			x1 *= 0.5;
			y1 *= 0.5;
			z1 *= 0.5;

			g_hull_size[i][0][0] = -x1;
			g_hull_size[i][0][1] = -y1;
			g_hull_size[i][0][2] = -z1;

			g_hull_size[i][1][0] = x1;
			g_hull_size[i][1][1] = y1;
			g_hull_size[i][1][2] = z1;
		}
	}

	fclose(file);
}
