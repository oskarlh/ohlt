#pragma once

#include "cmdlib.h"

class time_counter final {
    private:
        std::chrono::steady_clock::time_point startTime{std::chrono::steady_clock::time_point::min()};
        std::chrono::steady_clock::duration accumulated{0};
    public:
        void start() {
            if(startTime == std::chrono::steady_clock::time_point::min()) {
                startTime = std::chrono::steady_clock::now();
            }
        }

        time_counter(bool autoStart = true) {
            if(autoStart) {
                start();
            }
        }

        void stop() {
            if(startTime == std::chrono::steady_clock::time_point::min()) {
                return;
            }
            auto stopTime = std::chrono::steady_clock::now();
            accumulated += stopTime - startTime;
            startTime = std::chrono::steady_clock::time_point::min();
        }

        double getTotal() const {
            std::chrono::steady_clock::duration total = accumulated;
            if(startTime != std::chrono::steady_clock::time_point::min()) {
                total += std::chrono::steady_clock::now() - startTime;
            }
            return std::chrono::duration_cast<std::chrono::duration<double>>(total).count();
        }

        void reset() {
            startTime = std::chrono::steady_clock::time_point::min();
            accumulated = std::chrono::steady_clock::duration{0};
        }
};
