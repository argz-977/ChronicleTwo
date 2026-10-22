#pragma once

#include <filesystem>
#include <optional>
#include <string>

bool        FirstRunDataMissing(const std::filesystem::path &root);
std::string FirstRunDataMessage(const std::filesystem::path &root);

/**
 * Prepares missing data in a desktop setup window, or exits with kExitNoData if unavailable or cancelled. On iOS it
 * shows the launcher instead, every time, and returns once the player starts the game.
 */
void FirstRunRequireData(const std::filesystem::path &root, bool headless);

// iOS's launcher (ios_launcher.mm) finds the disc image and runs the extraction through these.

// The first disc image (.iso) in the app's Documents folder or its Inbox, where the Files app and Open In put it.
std::optional<std::filesystem::path> FirstRunDiscImage();

struct FirstRunExtraction {
    bool        running = false;
    float       fraction = 0;
    std::string stage;
    // Set once a finished extraction failed.
    std::string failure;
};

// Starts extracting image into root on a worker thread; does nothing while one is running.
void FirstRunExtractBegin(const std::filesystem::path &image, const std::filesystem::path &root);
// The running extraction's progress, or the outcome of the last one.
FirstRunExtraction FirstRunExtractPoll();
