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
#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <optional>
#include <thread>
#endif

#include "exitcodes.hpp"
#include "kazarov/data_version.hpp"
#include "log/log.hpp"
#include "platform/firstrun.hpp"
#ifdef SDL_PLATFORM_IOS
#include "platform/ios_launcher.hpp"
#endif
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
// The Files app lists the app's folder only once Documents holds something, so a note there shows the player
// where the disc image goes.
void IosLeaveNote() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }
    std::error_code error;
    const fs::path  documents = fs::path(home) / "Documents";
    fs::create_directories(documents, error);
    const fs::path note = documents / "Copy the Dark Chronicle ISO here.txt";
    if (!fs::exists(note, error)) {
        std::ofstream(note) << "Copy your Dark Chronicle disc image (.iso) into this folder, then open the game.\n";
    }
}

// The launcher's extraction: one worker at a time, whose failure stays readable until the next one starts.
std::thread       g_extract_worker;
std::atomic<bool> g_extract_running = false;
std::mutex        g_extract_mutex;
std::string       g_extract_failure;
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
#ifdef SDL_PLATFORM_IOS
    // An iOS app does not quit itself: the launcher stays up until the data is current and the player starts the
    // game.
    if (!headless) {
        IosLeaveNote();
        if (FirstRunDataMissing(root)) {
            CT_LOG_INFO(Main, "No current game data at {}: the launcher offers its extraction",
                        PathsDisplay(root));
        }
        IosLauncherRun(root);
        return;
    }
#endif
    if (!FirstRunDataMissing(root)) {
        return;
    }
    const std::string text = FirstRunDataMessage(root);
    // The logger ends each line itself.
    CT_LOG_ERROR(Main, "{}", std::string_view(text).substr(0, text.ends_with('\n') ? text.size() - 1 : text.size()));
    if (!headless) {
        try {
            const auto path = root.u8string();
            if (kazarov::setup_data("Dark Chronicle — Game data", std::string(
                                                                      reinterpret_cast<const char *>(path.data()), path.size())) &&
                !FirstRunDataMissing(root)) {
                return;
            }
        } catch (const std::exception &error) {
            CT_LOG_ERROR(Main, "Game data setup: {}", error.what());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dark Chronicle", error.what(), nullptr);
        }
    }
    LogFlush();
    std::exit(kExitNoData);
}

#ifdef SDL_PLATFORM_IOS
// iOS has no file dialog for the game to open: the disc image is copied into the app's Documents folder with the
// Files app, or chosen in the launcher. HOME is the app's container, also under LiveContainer, which points it at
// the guest app's own data container. A file shared to the app with Open In lands in an Inbox: Documents/Inbox
// when installed on its own, <home>/Inbox under LiveContainer.
std::optional<fs::path> FirstRunDiscImage() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return std::nullopt;
    }
    for (const fs::path &folder : {fs::path(home) / "Documents", fs::path(home) / "Documents" / "Inbox",
                                   fs::path(home) / "Inbox"}) {
        std::error_code error;
        for (const fs::directory_entry &entry : fs::directory_iterator(folder, error)) {
            std::string extension = entry.path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
            if (extension == ".iso" && entry.is_regular_file(error)) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

void FirstRunExtractBegin(const fs::path &image, const fs::path &root) {
    if (g_extract_running) {
        return;
    }
    if (g_extract_worker.joinable()) {
        g_extract_worker.join();
    }
    {
        const std::lock_guard lock(g_extract_mutex);
        g_extract_failure.clear();
    }
    g_extract_running = true;
    CT_LOG_INFO(Main, "Extracting the game data from {}", PathsDisplay(image));
    g_extract_worker = std::thread([image, root] {
        std::string failure;
        try {
            // Each worker holds its resources' converted output: two keep the peak well inside what iOS lets an
            // app use.
            const unsigned jobs = std::clamp(std::thread::hardware_concurrency(), 1u, 2u);
            kazarov::extract_tracked(image.string(), root.string(), jobs);
        } catch (const std::exception &error) {
            failure = error.what();
        }
        if (failure.empty() && FirstRunDataMissing(root)) {
            failure = "The extraction finished without current game data.";
        }
        if (!failure.empty()) {
            CT_LOG_ERROR(Main, "Game data setup: {}", failure);
        }
        {
            const std::lock_guard lock(g_extract_mutex);
            g_extract_failure = std::move(failure);
        }
        g_extract_running = false;
    });
}

FirstRunExtraction FirstRunExtractPoll() {
    FirstRunExtraction result;
    if (g_extract_running) {
        const kazarov::ExtractStatus status = kazarov::extract_status();
        result.running = true;
        result.fraction = std::clamp(status.fraction, 0.0f, 1.0f);
        result.stage = std::string(status.stage);
        return result;
    }
    if (g_extract_worker.joinable()) {
        g_extract_worker.join();
    }
    const std::lock_guard lock(g_extract_mutex);
    result.failure = g_extract_failure;
    return result;
}
#endif
