#include "hang_watchdog.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifndef _WIN32
#include <pthread.h>
#include <csignal>
#endif

#include "log/log.hpp"

namespace {

std::atomic<unsigned long long> g_frames = 0;

} // namespace

void HangWatchdogFrame() {
#ifndef _WIN32
    static const int seconds = [] {
        const char *setting = std::getenv("DC2_HANG_ABORT_SECONDS");
        return setting ? std::atoi(setting) : 0;
    }();
    if (seconds <= 0) {
        return;
    }
    static const bool started = [] {
        // The thread that ends frames is the one the watchdog aborts; the crash handler then runs on it and records
        // its stack.
        const pthread_t game = pthread_self();
        std::thread([game] {
            unsigned long long last = g_frames.load();
            auto               since = std::chrono::steady_clock::now();
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const unsigned long long now = g_frames.load();
                if (now != last) {
                    last = now;
                    since = std::chrono::steady_clock::now();
                } else if (std::chrono::steady_clock::now() - since >= std::chrono::seconds(seconds)) {
                    CT_LOG_ERROR(Main, "hang: no frame ended for {} s after frame {}; aborting the game thread",
                                 seconds, now);
                    LogFlush();
                    pthread_kill(game, SIGABRT);
                    return;
                }
            }
        }).detach();
        return true;
    }();
    (void)started;
    g_frames.fetch_add(1);
#endif
}
