#pragma once

#include <filesystem>

/**
 * Shows iOS's launcher until the player starts the game: the state of the game data under data_root, its extraction
 * from a disc image in Documents or one chosen in Files, and the APP SETTINGS rows. Returns once the data is current
 * and the player tapped Play. Called on the main thread, before the game's window exists.
 */
void IosLauncherRun(const std::filesystem::path &data_root);
