#include "common.h"

#include <cstring>

#include "stubs/pbuggy.hpp"

namespace {

/**
 * Associates a train-fight motion with its first frame in retail's animation data.
 */
struct RetailKey {
    const char *name;  /**< Shift-JIS name of the motion. */
    int         start; /**< First frame of the motion in retail's info.cfg. */
};

/**
 * Defines retail frame origins for Max's bomb lift and throw and Cedric's reload.
 */
constexpr RetailKey kRetailKeys[] = {
    {.name = "\x8e\x9d\x82\xbf\x8f\xe3\x82\xb0",         .start = 11},
    {.name = "\x94\x9a\x92\x65\x93\x8a\x82\xb0",         .start = 38},
    {.name = "\x94\x9a\x92\x65\x82\xf0\x8e\xe6\x82\xe9", .start = 9 },
};

} // namespace

float PortBuggyRetailFrame(CCharacter2 *character) {
    const float frame = character->GetNowFrame();
    const auto *key = character->now_key;
    if (key == nullptr) {
        return frame;
    }
    for (const auto &retail : kRetailKeys) {
        if (std::strcmp(key->name, retail.name) == 0) {
            // Native motion sets use consecutive intervals; the bomb events use retail frame origins.
            return frame - static_cast<float>(key->start_frame) + static_cast<float>(retail.start);
        }
    }
    return frame;
}
