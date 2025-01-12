#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <tuple>

enum class log_level {
	verbose,
	info,
	warning,
	error
};

// Necessary since std::format and std::vformat don't support the char8_t types
template<class FirstArg>
inline const FirstArg& if_u8string_view_then_make_it_a_char_string_view(const FirstArg& firstArg) noexcept {
//requires(!std::is_convertible_v<const FirstArg&, std::u8string_view>)
	return firstArg;
}
template<class FirstArg>
inline std::string_view if_u8string_view_then_make_it_a_char_string_view(const FirstArg& firstArg) noexcept
requires(std::is_convertible_v<const FirstArg&, std::u8string_view>) {
	std::u8string_view utf8{std::u8string_view{firstArg}};
	return std::string_view{(const char*) utf8.begin(), utf8.length()};
}
// Necessary since std::make_format_args doesn't accept rvalues
template<class X> inline const X& to_cref(const X& x) noexcept {
	return x;
}

class compilation_root_data {
	private:
		log_level minLogLevel{log_level::verbose};
		std::ofstream logFile;

		template<class... Args> void do_log(std::u8string_view formatString, const Args&... args) {
			std::string formatted{
				std::vformat(
					std::string_view((const char*) formatString.data(), formatString.size()),
					std::make_format_args(
						to_cref(
							if_u8string_view_then_make_it_a_char_string_view(args)
						)...
					)
				)
			};

			std::cout << formatted << (char) u8'\n' << std::flush;
			if(logFile.is_open()) {
				logFile.write(formatted.data(), formatted.length());
				logFile.write((const char *) u8"\n", 1);
				// TODO: Handle write errors
			}
		}

	public:
		template<log_level Level, class... Args> void log(std::u8string_view formatString, const Args&... args) {
			if(Level >= minLogLevel) {
				do_log(formatString, args...);
			}
		}

		void setLogLevel(log_level level) noexcept {
			minLogLevel = level;
		}
		bool writeToLogFile(const std::filesystem::path& path) {
			if(logFile.is_open()) [[unlikely]] {
				return false;
			}

			logFile.open(path, std::ios_base::out | std::ios_base::trunc);
			return logFile.is_open();
		}
};

class compilation_context {
	private:
		compilation_root_data* root;

	public:
		compilation_context(compilation_root_data& r) : root(&r) {}
		compilation_context(const compilation_context& other) = default;
		compilation_context& operator=(const compilation_context& other) = default;

		template<class... Args>
		void log_verbose(std::u8string_view formatString, const Args&... args) {
			root->log<log_level::verbose>(formatString, args...);
		}

		template<class... Args>
		void log_info(std::u8string_view formatString, const Args&... args) {
			root->log<log_level::info>(formatString, args...);
		}

		template<class... Args>
		void log_warning(std::u8string_view formatString, const Args&... args) {
			root->log<log_level::warning>(formatString, args...);
		}

		template<class... Args>
		void log_error(std::u8string_view formatString, const Args&... args) {
			root->log<log_level::error>(formatString, args...);
		}
};
