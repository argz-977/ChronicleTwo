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
// app. HOME is the app's container, also under LiveContainer, which points it at the guest app's own data
// container. A file shared to the app with Open In lands in an Inbox: Documents/Inbox when installed on its own,
// <home>/Inbox under LiveContainer.
std::optional<fs::path> IosDiscImage() {
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

// UIKit shows an alert only over a window of the app's scene, and the game's own window does not exist yet: the
// setup shows its messages over a window of its own, which stays up, black, while the data is extracted.
SDL_Window   *g_setup_window = nullptr;
SDL_Renderer *g_setup_renderer = nullptr;

void IosOpenSetupWindow() {
    if (g_setup_window == nullptr && SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        g_setup_window = SDL_CreateWindow("Dark Chronicle", 0, 0, SDL_WINDOW_FULLSCREEN);
    }
}

void IosMessage(SDL_MessageBoxFlags flags, const char *text) {
    IosOpenSetupWindow();
    SDL_ShowSimpleMessageBox(flags, "Dark Chronicle", text, g_setup_window);
}

// SDL's debug font has ASCII alone; the stage names end in an ellipsis.
std::string IosAscii(std::string_view text) {
    std::string ascii;
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x80) {
            ascii += c;
        }
    }
    return ascii;
}

// Draws the extraction's progress: its stage, a bar and the percentage.
void IosDrawProgress(const std::string &image, float fraction, const std::string &stage) {
    IosOpenSetupWindow();
    if (g_setup_renderer == nullptr && g_setup_window != nullptr) {
        g_setup_renderer = SDL_CreateRenderer(g_setup_window, nullptr);
    }
    if (g_setup_renderer == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(g_setup_renderer, &width, &height);
    // The debug font's glyphs are 8 pixels square; this scale makes lines about a thirtieth of the height.
    const float scale = std::max(1.0f, static_cast<float>(height) / 240.0f);
    const float w = static_cast<float>(width) / scale;
    const float h = static_cast<float>(height) / scale;
    SDL_SetRenderScale(g_setup_renderer, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(g_setup_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_setup_renderer);
    SDL_SetRenderScale(g_setup_renderer, scale, scale);
    auto centred = [&](const std::string &text, float y) {
        const float x = (w - static_cast<float>(text.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) / 2.0f;
        SDL_RenderDebugText(g_setup_renderer, std::max(0.0f, x), y, text.c_str());
    };
    SDL_SetRenderDrawColor(g_setup_renderer, 255, 255, 255, 255);
    centred("Extracting the game data from " + IosAscii(image), h * 0.30f);
    centred(IosAscii(stage), h * 0.42f);
    const SDL_FRect frame = {w * 0.15f, h * 0.52f, w * 0.70f, 10.0f};
    SDL_RenderRect(g_setup_renderer, &frame);
    const SDL_FRect bar = {frame.x + 2.0f, frame.y + 2.0f, (frame.w - 4.0f) * std::clamp(fraction, 0.0f, 1.0f),
                           frame.h - 4.0f};
    SDL_RenderFillRect(g_setup_renderer, &bar);
    centred(std::to_string(static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f)) + "%", h * 0.60f);
    SDL_SetRenderDrawColor(g_setup_renderer, 160, 160, 160, 255);
    centred("This takes several minutes: keep the game open and the screen on.", h * 0.75f);
    SDL_RenderPresent(g_setup_renderer);
}

void IosCloseSetupWindow() {
    if (g_setup_renderer != nullptr) {
        SDL_DestroyRenderer(g_setup_renderer);
        g_setup_renderer = nullptr;
    }
    if (g_setup_window != nullptr) {
        SDL_DestroyWindow(g_setup_window);
        g_setup_window = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

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

// Extracts the disc image on a worker thread while the main thread keeps UIKit's run loop turning, so the system
// does not take the app for hung. An iOS app does not quit itself: without a disc image, or after a failed
// extraction, the player is told what to do and the search starts again when they tap the button.
void IosSetupData(const fs::path &root) {
    IosLeaveNote();
    while (FirstRunDataMissing(root)) {
        const std::optional<fs::path> image = IosDiscImage();
        if (!image) {
            IosMessage(SDL_MESSAGEBOX_INFORMATION,
                       "No Dark Chronicle disc image found.\n\nCopy your disc image (.iso) into Files > On My "
                       "iPhone > Dark Chronicle, then tap OK to look again.\n\nUnder LiveContainer, copy it into "
                       "the game's data folder instead: LiveContainer's own folder, then Data > Application > the "
                       "game's folder > Documents.");
            continue;
        }
        // The screen must stay on through the extraction: a locked iPhone suspends the app.
        SDL_DisableScreenSaver();
        std::atomic<bool> done = false;
        std::string       failure;
        std::thread       worker([&] {
            try {
                // Each worker holds its resources' converted output: two keep the peak well inside what iOS lets an
                // app use.
                const unsigned jobs = std::clamp(std::thread::hardware_concurrency(), 1u, 2u);
                kazarov::extract_tracked(image->string(), root.string(), jobs);
            } catch (const std::exception &error) {
                failure = error.what();
            }
            done = true;
        });
        while (!done) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
            SDL_PumpEvents();
            const kazarov::ExtractStatus status = kazarov::extract_status();
            IosDrawProgress(image->filename().string(), status.fraction, std::string(status.stage));
        }
        worker.join();
        if (!failure.empty()) {
            CT_LOG_ERROR(Main, "Game data setup: {}", failure);
            IosMessage(SDL_MESSAGEBOX_ERROR, ("The game data could not be extracted:\n" + failure).c_str());
        }
    }
    IosCloseSetupWindow();
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
            IosSetupData(root);
            return;
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
#ifdef SDL_PLATFORM_IOS
            IosMessage(SDL_MESSAGEBOX_ERROR, error.what());
            IosCloseSetupWindow();
#else
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dark Chronicle", error.what(), nullptr);
#endif
        }
    }
    LogFlush();
    std::exit(kExitNoData);
}
