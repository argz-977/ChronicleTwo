#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "platform/config.hpp"
#include "platform/window.hpp"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace options {

// iOS has no window to size, no mouse and no DualSense speaker: there the page leaves those rows out and is
// called APP SETTINGS.
#if defined(__APPLE__) && TARGET_OS_IPHONE
inline constexpr bool kAppPage = true;
#else
inline constexpr bool kAppPage = false;
#endif
inline constexpr const char *kPageTitle = kAppPage ? "APP SETTINGS" : "PC SETTINGS";

struct Row {
    std::string                                key;
    std::string                                label;
    std::string                                help;
    std::function<std::string(const Config &)> value;
    std::function<void(Config &, int)>         step;
    std::string                                action;
};

struct Page {
    const char      *name;
    std::vector<Row> rows;
};

std::span<const Page>        Pages();
bool                         StepRow(const Row &row, Config &config, int direction);
bool                         SetBinding(Config &config, std::string_view action, std::string_view key);
void                         ResetBinding(Config &config, std::string_view action);
std::vector<DisplayModeSize> ResolutionList(std::span<const DisplayModeSize> modes, int width, int height);
void                         ListResolutions();
// Names for the PC page's prompts, including keyboard rebinding and forced controller families.
std::string ButtonName(std::string_view action, const Config &config);

} // namespace options
