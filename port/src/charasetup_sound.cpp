#include "common.h"

#include <array>
#include <format>
#include <span>
#include <string>

#include "bounded_text.hpp"
#include "charasetup.hpp"
#include "userdata.hpp"

namespace {

/**
 * Bytes of the smallest sound path a caller passes (the event script's character sound lookup).
 */
constexpr std::size_t kSoundPathCapacity = 0x28;

} // namespace

// Retail formats the ridepod's sound through "%s" from a name GetRoboSoundFileName leaves unwritten when
// the arm has no part record, and writes every path without a bound.
PORT_OVERRIDE void GetCharacterSnd(CUserDataManager *user_data, int unit, char *path) {
    if (path == nullptr) {
        return;
    }
    path[0] = '\0';
    if (user_data == nullptr) {
        return;
    }
    std::string sound;
    if (unit == USER_CHARA_MAX) {
        const CHARA_DATA *chara = user_data->GetCharaDataPtr(unit);
        if (chara == nullptr) {
            return;
        }
        const int item_no = chara->equip[CHARA_EQUIP_SUB].item_no;
        if (item_no < 0x16 || item_no > 0x28) {
            sound = "snd2/chara/CH_000.sound.json";
        } else {
            sound = std::format("snd2/chara/CH_{:03}.sound.json", item_no - 0x16);
        }
    }
    if (unit == USER_CHARA_MONICA) {
        sound = "snd2/chara/CH_020.sound.json";
    }
    if (unit == USER_CHARA_ROBO) {
        std::array<char, 0x40> name{};
        user_data->robo_data.parts[ROBO_PART_ARM].GetRoboSoundFileName(name.data());
        if (name[0] != '\0') {
            sound = std::format("snd2/chara/{}.sound.json", bounded_text::View(name.data(), name.size()));
        }
    }
    if (!sound.empty()) {
        bounded_text::Copy(std::span(path, kSoundPathCapacity), sound);
    }
}
