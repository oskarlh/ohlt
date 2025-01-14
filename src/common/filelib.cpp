#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>


#include "cmdlib.h"
#include "messages.h"
#include "log.h"
#include "mathtypes.h"
#include "mathlib.h"

using namespace std::literals;


// <Opened successfully>, <Size of file>, <File stream>
std::tuple<bool, std::size_t, std::ifstream>
static open_file_and_get_size(const std::filesystem::path& filePath) {
    // Open the file and get the file size
	std::ifstream file{filePath.c_str(), std::ios::ate | std::ios::binary | std::ios::in};
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios_base::beg);
	return std::make_tuple(file.good(), fileSize, std::move(file));
}


// Success, number of elements, elements
std::tuple<bool, std::size_t, std::unique_ptr<std::byte[]>>
// TODO: Add restrictions to make sure Element is a POD-like type
read_binary_file(const std::filesystem::path& filePath) {
	auto [fileOpened, fileSize, file] = open_file_and_get_size(filePath);
	if(!fileOpened) {
        return {false, 0, nullptr};
	}

    auto buffer = std::make_unique_for_overwrite<std::byte[]>(fileSize);
    file.read((char*) buffer.get(), fileSize);

    if(file.eof()) [[unlikely]] {
        // The file shrunk between tellg() and the read
        fileSize = file.gcount();

        auto smallerBuffer = std::make_unique_for_overwrite<std::byte[]>(fileSize);
        std::move(buffer.get(), buffer.get() + fileSize, smallerBuffer.get());
		std::swap(buffer, smallerBuffer);
    } else if(!file.good()) {
        return {false, 0, nullptr};
    }

    return { true, fileSize, std::move(buffer) };
}

std::optional<std::u8string> read_utf8_file(const std::filesystem::path& filePath, bool windowsLineEndingsToUnix) {
	auto [fileOpened, fileSize, file] = open_file_and_get_size(filePath);
	if(!fileOpened) {
        return std::nullopt;
	}

	std::u8string text;
    text.resize_and_overwrite(fileSize, [&file](char8_t* buffer, std::size_t bufferSize) {
        file.read((char*) buffer, bufferSize);

        if(file.eof()) [[unlikely]] {
             // In case the file shrunk between tellg() and the read
            file.clear();
            return std::size_t(file.gcount());
        }

        return bufferSize;
    });
    if(!file.good()) {
        return std::nullopt;
    }

    if(windowsLineEndingsToUnix) {
        std::size_t outIndex = 0;
        for(
            std::u8string_view remainingCharactersToCopy{text};
            !remainingCharactersToCopy.empty();
            remainingCharactersToCopy = remainingCharactersToCopy.substr(1)
        ) {
            if (remainingCharactersToCopy.starts_with(u8"\r\n"sv)) {
                remainingCharactersToCopy = remainingCharactersToCopy.substr(1);
            }
            text[outIndex++] = remainingCharactersToCopy[0];
        }
        text.resize(outIndex);
    }

    // Don't waste space if the file shrunk between tellg() and read()
    // or if Windows line endings were replaced
    text.shrink_to_fit();

    return text;
}

std::optional<std::filesystem::path> try_to_get_canonical_path(const std::filesystem::path& path) {
    std::error_code canonicalErrorCode;
    const std::filesystem::path canonicalPath = std::filesystem::canonical(
        path,
        canonicalErrorCode
    );
    if(canonicalErrorCode) {
        return std::nullopt;
    }
    return canonicalPath;
}

#ifdef VERSION_LINUX
std::optional<std::filesystem::path> try_to_get_executable_path_linux() {
  return try_to_get_canonical_path("/proc/self/exe");
}
#endif


#ifdef VERSION_MACOS
#include <mach-o/dyld.h>
#include <utility>

std::optional<std::filesystem::path> try_to_get_executable_path_macos() {
  std::string pathString;
  std::uint32_t sizeToTry = (1 << 8) - 1;
  int errorCode = 0;
  bool reachedSizeLimit = false;
  do {
    sizeToTry = sizeToTry * 2 + 1;
    if(std::cmp_greater_equal(sizeToTry, pathString.max_size())) {
        sizeToTry = (std::uint32_t) pathString.max_size();
        reachedSizeLimit = true;
    }
    if (sizeToTry == std::numeric_limits<std::uint32_t>::max()) {
        reachedSizeLimit = true;
    }

    pathString.resize_and_overwrite(
        sizeToTry,
        [&errorCode] (char* buffer, std::size_t bufferSize) constexpr{
            std::uint32_t resultLength = bufferSize;
            errorCode = _NSGetExecutablePath(buffer, &resultLength);
            return (std::size_t) resultLength;
    });
    // errorCode == -1 means the buffer wasn't large enough to store the string
  } while(errorCode == -1 && !reachedSizeLimit);
    if(errorCode !=0) {
        return std::nullopt;
    }

    return try_to_get_canonical_path(pathString);
}
#endif

#ifdef SYSTEM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

std::optional<std::filesystem::path> try_to_get_executable_path_win32() {
  std::wstring pathString;
  std::size_t nextSizeAttempt = (1 << 8) - 1;
  bool tooSmall;
  bool reachedSizeLimit = false;
  do {
    nextSizeAttempt = nextSizeAttempt * 2 + 1;
    if(std::cmp_greater_equal(sizeToTry, pathString.max_size())) {
        sizeToTry = (DWORD) pathString.max_size();
        reachedSizeLimit = true;
    }
    if (sizeToTry == std::numeric_limits<DWORD>::max()) {
        reachedSizeLimit = true;
    }

    tooSmall = false;
    pathString.resize_and_overwrite(
        nextSizeAttempt,
        [&errorCode] (wchar_t* buffer, std::size_t bufferSize) constexpr{
            DWORD resultLength = GetModuleFileNameW(nullptr, buffer, (DWORD) bufferSize);
            if(resultLength == bufferSize) {
                tooSmall = true;
                return 0;
            }
            return (std::size_t) resultLength;
    });
  } while(tooSmall && !reachedSizeLimit);
    if(pathString.empty()) {
        return std::nullopt;
    }

    return try_to_get_canonical_path(pathString);
}
#endif

std::filesystem::path get_path_to_directory_with_executable(char** argvForFallback)
{  
    // argv0 may be nulltpr, because while argv[0] is guaranteed by the standard to be available,
    // in the case of argc == 0, this is also true: arg[0] == nullptr
    const char* argv0 = argvForFallback[0];

    const std::optional<std::filesystem::path> executablePath = std::optional<std::filesystem::path>{}
#ifdef VERSION_LINUX
    .or_else(try_to_get_executable_path_linux)
#endif
#ifdef VERSION_MACOS
    .or_else(try_to_get_executable_path_macos)
#endif
#ifdef SYSTEM_WIN32
    .or_else(try_to_get_executable_path_windows)
#endif
    .or_else(
       [argv0] () -> std::optional<std::filesystem::path> {
          if(argv0 == nullptr || argv0[0] == '\0') {
            return std::nullopt;
          }
          return std::filesystem::canonical(argv0);
       }
    );

    const std::filesystem::path parentFolderPath = executablePath
    .transform([] (const std::filesystem::path& exec) {
        return exec.parent_path();
    }).or_else(
        []() { return std::make_optional(std::filesystem::current_path()); }
    ).value();
    
    return try_to_get_canonical_path(parentFolderPath).or_else(
        []() { return std::make_optional(std::filesystem::current_path()); }
    ).value();
}



std::filesystem::path filename_from_file_path_string(std::u8string_view filePathString) {
    std::u8string_view withoutWindowsDirectorySepartors{filePathString.substr(filePathString.find_last_of(u8'\\') + 1zu)};
    return std::filesystem::path(withoutWindowsDirectorySepartors, std::filesystem::path::generic_format).filename();
}

/*
 * ================
 * filelength
 * ================
 */
int             q_filelength(FILE* f)
{
    int             pos;
    int             end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

FILE*           SafeOpenWrite(const char* const filename)
{
    FILE*           f;

    f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, std::strerror(errno));

    return f;
}

FILE*           SafeOpenRead(const std::filesystem::path& filename)
{
    FILE*           f;

    f = fopen(filename.c_str(), "rb");

    if (!f)
        Error("Error opening %s: %s", filename.c_str(), strerror(errno));

    return f;
}

void SafeRead(FILE* f, void* buffer, int count)
{
    if (fread(buffer, 1, count, f) != (size_t) count)
        Error("File read failure");
}

void SafeWrite(FILE* f, const void* const buffer, int count)
{
    if (fwrite(buffer, 1, count, f) != (size_t) count)
        Error("File write failure"); //Error("File read failure"); //--vluzacn
}


/*
 * ==============
 * SaveFile
 * ==============
 */
void            SaveFile(const char* const filename, const void* const buffer, int count)
{
    FILE*           f;

    f = SafeOpenWrite(filename);
    SafeWrite(f, buffer, count);
    fclose(f);
}

