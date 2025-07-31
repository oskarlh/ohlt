#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

enum class log_level {
	verbose,
	info,
	warning,
	error
};

// Necessary since std::format and std::vformat don't support the char8_t
// types
template <class FirstArg>
inline FirstArg const &
if_u8string_view_then_make_it_a_char_string_view(FirstArg const & firstArg
) noexcept {
	// requires(!std::is_convertible_v<const FirstArg&, std::u8string_view>)
	return firstArg;
}

template <class FirstArg>
inline std::string_view
if_u8string_view_then_make_it_a_char_string_view(FirstArg const & firstArg
) noexcept
	requires(std::is_convertible_v<FirstArg const &, std::u8string_view>) {
	std::u8string_view utf8{ std::u8string_view{ firstArg } };
	return std::string_view{ (char const *) utf8.begin(), utf8.length() };
}

// Necessary since std::make_format_args doesn't accept rvalues
template <class X>
inline X const & to_cref(X const & x) noexcept {
	return x;
}

class compilation_root_data final {
  private:
	log_level minLogLevel{ log_level::verbose };
	std::ofstream logFile;

	template <class... Args>
	void do_log(std::u8string_view formatString, Args const &... args) {
		std::string formatted{ std::vformat(
			std::string_view(
				(char const *) formatString.data(), formatString.size()
			),
			std::make_format_args(to_cref(
				if_u8string_view_then_make_it_a_char_string_view(args)
			)...)
		) };

		// TODO: Find a way to always flush the logs on exit or if there's
		// an error using set_terminate, atexit, at_quick_exit. Also, for
		// threads, consider using std::osyncstream if they ever need to log

		std::cout << formatted << (char) u8'\n';
		if (logFile.is_open()) {
			logFile.write(formatted.data(), formatted.length());
			logFile.write((char const *) u8"\n", 1);
			// TODO: Handle write errors
		}
	}

  public:
	template <log_level Level, class... Args>
	void log(std::u8string_view formatString, Args const &... args) {
		if (Level >= minLogLevel) {
			do_log(formatString, args...);
		}
	}

	void setLogLevel(log_level level) noexcept {
		minLogLevel = level;
	}

	bool writeToLogFile(std::filesystem::path const & path) {
		if (logFile.is_open()) [[unlikely]] {
			return false;
		}

		logFile.open(path, std::ios_base::out | std::ios_base::trunc);
		return logFile.is_open();
	}
};

class compilation_context final {
  private:
	compilation_root_data* root;

  public:
	compilation_context(compilation_root_data& r) : root(&r) { }

	compilation_context(compilation_context const & other) = default;
	compilation_context& operator=(compilation_context const & other
	) = default;

	template <class... Args>
	void
	log_verbose(std::u8string_view formatString, Args const &... args) {
		root->log<log_level::verbose>(formatString, args...);
	}

	template <class... Args>
	void log_info(std::u8string_view formatString, Args const &... args) {
		root->log<log_level::info>(formatString, args...);
	}

	template <class... Args>
	void
	log_warning(std::u8string_view formatString, Args const &... args) {
		root->log<log_level::warning>(formatString, args...);
	}

	template <class... Args>
	void log_error(std::u8string_view formatString, Args const &... args) {
		root->log<log_level::error>(formatString, args...);
	}
};
