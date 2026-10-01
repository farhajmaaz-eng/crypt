#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace cryptui {

class Spinner {
private:
    std::atomic<bool> running{false};
    std::thread worker;
    std::string message;

public:
    explicit Spinner(const std::string& text)
        : message(text) {}

    void start() {
        running = true;

        worker = std::thread([this]() {
            const char frames[] = {'|', '/', '-', '\\'};
            std::size_t frame = 0;

            while (running) {
                std::cout << "\r"
                          << message
                          << " "
                          << frames[frame++ % 4]
                          << std::flush;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );
            }

            std::cout << "\r\033[2K" << std::flush;
        });
    }

    void stop() {
        running = false;

        if (worker.joinable())
            worker.join();
    }

    ~Spinner() {
        stop();
    }
};

inline void success(const std::string& package) {
    std::cout << "✓ Added " << package << "\n";
}

inline void failure(const std::string& package) {
    std::cout << "✗ Failed to add " << package << "\n";
    std::cout << "Run again with --verbose for details.\n";
}

}
