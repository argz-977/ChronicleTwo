// nlohmann must precede the game headers, whose macros it would otherwise see.
// clang-format off
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"

#include <array>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "mainloop.hpp"
#include "npccfg.hpp"
#include "stubs/npccfg.hpp"

namespace {

/**
 * One party character as a party_characters document lists it.
 */
struct PartyCharacter {
    int                        id;            /**< Party character number. */
    std::optional<std::string> name;          /**< CP932 display name, or nothing to keep the entry's name. */
    std::optional<std::string> model;         /**< CP932 model base name, or nothing to keep the entry's model. */
    int                        unk_31;        /**< Value stored in NPC_BASE_DATA::unk_31. */
    int                        ability_num;   /**< Number of abilities. */
    int                        max_npc_point; /**< Points the abilities draw on. */
    std::array<int, 4>         ability_cost;  /**< Points each ability uses. */
    int                        debug_flag;    /**< Whether the debug party-character selector lists the character. */
};

/**
 * Reads a nullable string member, refusing text that does not fit a fixed character field.
 */
std::optional<std::string> FieldText(const native_config::Json &object, const char *key, std::size_t capacity) {
    auto text = native_config::GameText(native_config::At(object, key));
    if (text && text->size() >= capacity) {
        throw std::runtime_error(std::string(key) + " is longer than its " + std::to_string(capacity - 1) + "-byte field");
    }
    return text;
}

/**
 * Parses a party_characters document.
 */
std::vector<PartyCharacter> ParsePartyCharacters(const native_config::Json &document) {
    using namespace native_config;
    std::vector<PartyCharacter> characters;
    for (const auto &character : Array(document, "characters")) {
        characters.push_back({
            .id = Int(character, "id"),
            .name = FieldText(character, "name", sizeof(NPC_BASE_DATA::name)),
            .model = FieldText(character, "model", sizeof(NPC_BASE_DATA::model)),
            .unk_31 = Int(character, "unk_31"),
            .ability_num = Int(character, "ability_num"),
            .max_npc_point = Int(character, "max_npc_point"),
            .ability_cost = Ints<4>(character, "ability_cost"),
            .debug_flag = Int(character, "debug_flag"),
        });
    }
    return characters;
}

/**
 * Stores one party character in its table entry as the NPC_INFO callback does.
 */
void ApplyPartyCharacter(NPC_BASE_DATA &data, const PartyCharacter &character) {
    data.chara_no = static_cast<s16>(character.id);
    if (character.name) {
        std::memcpy(data.name, character.name->c_str(), character.name->size() + 1);
    }
    if (character.model) {
        std::memcpy(data.model, character.model->c_str(), character.model->size() + 1);
    }
    data.unk_31 = static_cast<s8>(character.unk_31);
    data.ability_num = static_cast<s8>(character.ability_num);
    data.max_npc_point = static_cast<s8>(character.max_npc_point);
    for (std::size_t index = 0; index < character.ability_cost.size(); ++index) {
        data.ability_cost[index] = static_cast<u8>(character.ability_cost[index]);
    }
    data.debug_flag = static_cast<s8>(character.debug_flag);
}

} // namespace

PORT_OVERRIDE void LoadNPCCfg() {
    const auto  path = std::format("npc{}.cfg.json", LanguageCode);
    std::size_t count = 0;
    if (const auto characters = native_config::ReadGame<std::vector<PartyCharacter> >(path.c_str(), "party_characters", ParsePartyCharacters)) {
        for (const auto &character : *characters) {
            // Retail writes past the table for an over-long list; the port keeps the entries that fit.
            if (count >= std::size(NpcBaseData)) {
                break;
            }
            ApplyPartyCharacter(NpcBaseData[count++], character);
        }
    }
    NpcBaseDataTotalNum = static_cast<int>(count);
}
