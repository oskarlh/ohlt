#include "hlrad.h"
#include "log.h"

/*
 * =============
 * writetransfers
 * =============
 */

void writetransfers(
	char const * const transferfile, long const total_patches
) {
	FILE* file;

	file = fopen(transferfile, "w+b");
	if (file != nullptr) {
		unsigned amtwritten;

		Log("Writing transfers file [%s]\n", transferfile);

		amtwritten = fwrite(&total_patches, sizeof(total_patches), 1, file);
		if (amtwritten != 1) {
			goto FailedWrite;
		}

		long patchcount = total_patches;
		for (std::vector<patch_t>::iterator patch = g_patches.begin();
		     patchcount-- > 0;
		     ++patch) {
			amtwritten = fwrite(
				&patch->iIndex, sizeof(patch->iIndex), 1, file
			);
			if (amtwritten != 1) {
				goto FailedWrite;
			}

			if (patch->iIndex) {
				amtwritten = fwrite(
					patch->tIndex,
					sizeof(transfer_index_t),
					patch->iIndex,
					file
				);
				if (amtwritten != patch->iIndex) {
					goto FailedWrite;
				}
			}

			amtwritten = fwrite(
				&patch->iData, sizeof(patch->iData), 1, file
			);
			if (amtwritten != 1) {
				goto FailedWrite;
			}
			if (patch->iData) {
				if (g_rgb_transfers) {
					amtwritten = fwrite(
						patch->tRGBData,
						vector_size[(std::size_t
					    ) g_rgbtransfer_compress_type],
						patch->iData,
						file
					);
				} else {
					amtwritten = fwrite(
						patch->tData,
						float_size[(std::size_t) g_transfer_compress_type],
						patch->iData,
						file
					);
				}
				if (amtwritten != patch->iData) {
					goto FailedWrite;
				}
			}
		}

		fclose(file);
	} else {
		Error(
			"Failed to open incremenetal file [%s] for writing\n",
			transferfile
		);
	}
	return;

FailedWrite:
	fclose(file);
	std::filesystem::remove(transferfile);
	// Warning("Failed to generate incremental file [%s] (probably ran out
	// of disk space)\n");
	Warning(
		"Failed to generate incremental file [%s] (probably ran out of disk space)\n",
		transferfile
	); //--vluzacn
}

/*
 * =============
 * readtransfers
 * =============
 */

bool readtransfers(char const * const transferfile, long const numpatches) {
	FILE* file = fopen(transferfile, "rb");
	if (file != nullptr) {
		Log("Reading transfers file [%s]\n", transferfile);

		long total_patches;
		unsigned amtread = fread(
			&total_patches, sizeof(total_patches), 1, file
		);
		if (amtread != 1) {
			goto FailedRead;
		}
		if (total_patches != numpatches) {
			goto FailedRead;
		}

		long patchcount = total_patches;
		for (; patchcount-- > 0;) {
			patch_t* patch = &g_patches.emplace_back();
			amtread = fread(&patch->iIndex, sizeof(patch->iIndex), 1, file);
			if (amtread != 1) {
				goto FailedRead;
			}
			if (patch->iIndex) {
				patch->tIndex = new transfer_index_t[patch->iIndex]();
				hlassume(patch->tIndex != nullptr, assume_NoMemory);
				amtread = fread(
					patch->tIndex,
					sizeof(transfer_index_t),
					patch->iIndex,
					file
				);
				if (amtread != patch->iIndex) {
					goto FailedRead;
				}
			}

			amtread = fread(&patch->iData, sizeof(patch->iData), 1, file);
			if (amtread != 1) {
				goto FailedRead;
			}
			if (patch->iData) {
				if (g_rgb_transfers) {
					patch->tRGBData = (rgb_transfer_data_t*) new std::byte
						[patch->iData
					         * vector_size[(std::size_t
					         ) g_rgbtransfer_compress_type]
					     + unused_size]();
					hlassume(patch->tRGBData != nullptr, assume_NoMemory);
					amtread = fread(
						patch->tRGBData,
						vector_size[(std::size_t
					    ) g_rgbtransfer_compress_type],
						patch->iData,
						file
					);
				} else {
					patch->tData = (transfer_data_t*) new std::byte
						[patch->iData
					         * float_size[(std::size_t
					         ) g_transfer_compress_type]
					     + unused_size]();
					hlassume(patch->tData != nullptr, assume_NoMemory);
					amtread = fread(
						patch->tData,
						float_size[(std::size_t) g_transfer_compress_type],
						patch->iData,
						file
					);
				}
				if (amtread != patch->iData) {
					goto FailedRead;
				}
			}
		}

		fclose(file);
		// Warning("Finished reading transfers file [%s] %d\n",
		// transferfile);
		Warning(
			"Finished reading transfers file [%s]\n", transferfile
		); //--vluzacn
		return true;
	}
	Warning("Failed to open transfers file [%s]\n", transferfile);
	return false;

FailedRead: {
	for (patch_t& patch : g_patches) {
		delete[] patch.tData;
		patch.tData = nullptr;
		delete[] patch.tIndex;
		patch.tIndex = nullptr;

		patch.iData = 0;
		patch.iIndex = 0;
	}
}
	fclose(file);
	std::filesystem::remove(transferfile);
	return false;
}
