#include "kazarov_bridge/rust/lib.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#ifdef SDL_PLATFORM_IOS
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <thread>
#endif

#include "exitcodes.hpp"
#include "kazarov/data_version.hpp"
#include "log/log.hpp"
#include "platform/firstrun.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

namespace {

std::string FlatpakHint(const std::string &where) {
    const char *id = std::getenv("FLATPAK_ID");
    if (id == nullptr || *id == '\0') {
        return "";
    }
    return "\nInside the Flatpak, from a shell: flatpak run --filesystem=<folder of the image>:ro --command=dc2data " +
           std::string(id) + " extract <disc image> " + where + "\n";
}

#ifdef SDL_PLATFORM_IOS
// iOS has no setup window or file dialog: the disc image is copied into the app's Documents folder with the Files
// app, which is the home directory's Documents.
std::optional<fs::path> IosDiscImage() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return std::nullopt;
    }
    std::error_code error;
    for (const fs::directory_entry &entry : fs::directory_iterator(fs::path(home) / "Documents", error)) {
        std::string extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension == ".iso" && entry.is_regular_file(error)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

// Extracts on a worker thread while the main thread keeps UIKit's run loop turning, so the system does not take
// the app for hung. False when there is no disc image to extract.
bool IosSetupData(const fs::path &root) {
    const std::optional<fs::path> image = IosDiscImage();
    if (!image) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Dark Chronicle",
                                 "Copy your Dark Chronicle disc image (.iso) into Files > On My iPhone > Dark Chronicle, "
                                 "then open the game again.",
                                 nullptr);
        return false;
    }
    const std::string notice = "Extracting the game data from " + image->filename().string() +
                               ". This takes several minutes: keep the game open and the screen on.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Dark Chronicle", notice.c_str(), nullptr);
    std::atomic<bool> done = false;
    std::string       failure;
    std::thread       worker([&] {
        try {
            const unsigned jobs = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
            kazarov::extract(image->string(), root.string(), jobs);
        } catch (const std::exception &error) {
            failure = error.what();
        }
        done = true;
    });
    while (!done) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    }
    worker.join();
    if (!failure.empty()) {
        throw std::runtime_error(failure);
    }
    return true;
}
#endif

} // namespace

bool FirstRunDataMissing(const fs::path &root) {
    // The data is <game-root>/data.zip, or the unified data/ directory.
    std::error_code error;
    const auto      game_root = root.parent_path();
    const bool      archive = fs::is_regular_file(game_root / "data.zip", error);
    const bool      directories = fs::is_directory(root, error);
    if ((!archive && !directories) || !fs::is_regular_file(game_root / "data.json", error)) {
        return true;
    }
    std::ifstream marker(game_root / "data.json");
    const auto    parsed = nlohmann::json::parse(marker, nullptr, false);
    return !parsed.is_object() || !parsed.contains("schema_version") || !parsed["schema_version"].is_number_integer() ||
           parsed["schema_version"] != 1 || !parsed.contains("data_version") ||
           !parsed["data_version"].is_number_integer() || parsed["data_version"] != KAZAROV_DATA_VERSION;
}

std::string FirstRunDataMessage(const fs::path &root) {
    const std::string where = PathsDisplay(root);
    return "No current Dark Chronicle data at " + where + ".\n\n"
                                                          "Extract it once from the disc with:\n"
                                                          "  dc2data extract <disc image or folder> " +
           where + "\n\n"
                   "The source is an image of the Dark Chronicle disc, or a folder holding DATA.HD4, DATA.DAT, SOUND.HD3 and SOUND.DAT.\n\n"
                   "Or point the game at an existing extraction with --data <dir> or the DC2_DATA environment variable.\n" +
           FlatpakHint(where);
}

void FirstRunRequireData(const fs::path &root, bool headless) {
    if (!FirstRunDataMissing(root)) {
        return;
    }
    const std::string text = FirstRunDataMessage(root);
    // The logger ends each line itself.
    CT_LOG_ERROR(Main, "{}", std::string_view(text).substr(0, text.ends_with('\n') ? text.size() - 1 : text.size()));
    if (!headless) {
        try {
#ifdef SDL_PLATFORM_IOS
            if (IosSetupData(root) && !FirstRunDataMissing(root)) {
                return;
            }
#else
            const auto path = root.u8string();
            if (kazarov::setup_data("Dark Chronicle — Game data", std::string(
                                                                      reinterpret_cast<const char *>(path.data()), path.size())) &&
                !FirstRunDataMissing(root)) {
                return;
            }
#endif
        } catch (const std::exception &error) {
            CT_LOG_ERROR(Main, "Game data setup: {}", error.what());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dark Chronicle", error.what(), nullptr);
        }
    }
    LogFlush();
    std::exit(kExitNoData);
}
