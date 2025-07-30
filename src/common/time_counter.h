#pragma once
#include <chrono>

class time_counter final {
  private:
	std::chrono::steady_clock::time_point startTime{
		std::chrono::steady_clock::time_point::min()
	};
	std::chrono::steady_clock::duration accumulated{ 0 };

  public:
	void start() noexcept {
		if (startTime == std::chrono::steady_clock::time_point::min()) {
			startTime = std::chrono::steady_clock::now();
		}
	}

	time_counter(bool autoStart = true) noexcept {
		if (autoStart) {
			start();
		}
	}

	void stop() noexcept {
		if (startTime == std::chrono::steady_clock::time_point::min()) {
			return;
		}
		auto stopTime = std::chrono::steady_clock::now();
		accumulated += stopTime - startTime;
		startTime = std::chrono::steady_clock::time_point::min();
	}

	double get_total() const noexcept {
		std::chrono::steady_clock::duration total = accumulated;
		if (startTime != std::chrono::steady_clock::time_point::min()) {
			total += std::chrono::steady_clock::now() - startTime;
		}
		return std::chrono::duration_cast<std::chrono::duration<double>>(
				   total
		)
			.count();
	}

	void reset() noexcept {
		startTime = std::chrono::steady_clock::time_point::min();
		accumulated = std::chrono::steady_clock::duration{ 0 };
	}

	void restart() noexcept {
		reset();
		start();
	}
};
