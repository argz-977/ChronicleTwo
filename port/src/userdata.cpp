#include "common.h"

#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "bounded_text.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "menucls1.hpp"
#include "menucommon.hpp"
#include "userdata.hpp"

namespace {

/**
 *
 * Gives a table's entry for the current language, falling back to the first entry outside the table.
 *
 */
template <typename Entry, std::size_t Count>
const Entry &LanguageEntry(const std::array<Entry, Count> &table) {
    return LanguageCode >= 0 && static_cast<std::size_t>(LanguageCode) < Count ? table[LanguageCode] : table[0];
}

/**
 *
 * Gives the renameable name field of an owned item, or an empty span when its family has none.
 *
 */
std::span<char> RenameField(CGameDataUsed &item) {
    switch (item.used_type) {
        case USED_ITEM_TYPE_WEAPON:
            return item.data.weapon.name;
        case USED_ITEM_TYPE_ROBO_PART:
            return item.data.robopart.name;
        case USED_ITEM_TYPE_FISH:
            return item.data.fish.name;
        case USED_ITEM_TYPE_ATTACH:
            return item.data.attach.name;
        default:
            return {};
    }
}

/**
 *
 * Gives an item's message name, or nothing when the item has no message.
 *
 */
std::optional<std::string_view> MessageName(int item_no) {
    const char *message = GetItemMessage(item_no);
    if (message == nullptr) {
        return std::nullopt;
    }
    return std::string_view(message);
}

/**
 *
 * Gives the name an owned item is shown with, bounded by the field it is stored in.
 *
 */
std::optional<std::string_view> StoredName(CGameDataUsed &item) {
    switch (item.used_type) {
        case USED_ITEM_TYPE_WEAPON:
            return bounded_text::View(item.data.weapon.name, sizeof item.data.weapon.name);
        case USED_ITEM_TYPE_ROBO_PART:
            return bounded_text::View(item.data.robopart.name, sizeof item.data.robopart.name);
        case USED_ITEM_TYPE_FISH:
            return bounded_text::View(item.data.fish.name, sizeof item.data.fish.name);
        case USED_ITEM_TYPE_ATTACH:
            if (item.item_no == ITEM_ID_SPECTOL) {
                return bounded_text::View(item.data.attach.name, sizeof item.data.attach.name);
            }
            return MessageName(item.item_no);
        case USED_ITEM_TYPE_BOILED:
            return bounded_text::View(item.data.boiled.name, sizeof item.data.boiled.name);
        default:
            return MessageName(item.item_no);
    }
}

/**
 *
 * Words a cooked fish's name is written with in one language.
 *
 */
struct BoiledName {
    std::string_view prefix; /**< Text written before the fish's name. */
    bool             named;  /**< Whether the fish's name follows the prefix. */
};

} // namespace

// Retail appends the level through sprintf with the item's name as the format, so a renamed weapon
// whose name holds '%' ran arbitrary conversions, and long names overran the static result.
PORT_OVERRIDE char *CGameDataUsed::GetName(int name_type) {
    static char word[0x61];
    // Opening and closing quotation marks for each language, plain and renamed.
    static constexpr std::array<std::array<std::array<std::string_view, 2>, 2>, 8> symbol_tbl{
        {
         {{{"\x81w", "\x81x"}, {"\x81y", "\x81z"}}},
         {{{"\"", "\""}, {"\x81y", "\x81z"}}},
         {{{"\"", "\""}, {"'", "'"}}},
         {{{"\"", "\""}, {"'", "'"}}},
         {{{"\"", "\""}, {"'", "'"}}},
         {{{"\"", "\""}, {"'", "'"}}},
         {{{"\"", "\""}, {"'", "'"}}},
         {{{"\"", "\""}, {"\x81y", "\x81z"}}},
         }
    };

    const auto  name = StoredName(*this);
    const auto &quotes = LanguageEntry(symbol_tbl)[rename_flag != 0 ? 1 : 0];
    std::string text;

    if (name) {
        if (name_type == ITEM_NAME_TYPE_QUOTED) {
            text = quotes[0];
            text += *name;
        }
        if (name_type == ITEM_NAME_TYPE_PLAIN || name_type == ITEM_NAME_TYPE_LEVEL) {
            text = *name;
        }
    }

    if (name_type > ITEM_NAME_TYPE_PLAIN && (used_type == USED_ITEM_TYPE_WEAPON || used_type == USED_ITEM_TYPE_ATTACH)) {
        const int level = GetLevel();
        if (level > 0) {
            if (LanguageCode > 0) {
                text += std::format(" + {}", level);
            } else {
                // Japanese writes the level with full-width digits after a full-width plus.
                text += "\x81{";
                for (const char digit : std::to_string(level)) {
                    text += bounded_text::View(MenuBigNum[digit - '0']);
                }
            }
        }
    }

    if (name && name_type == ITEM_NAME_TYPE_QUOTED) {
        text += quotes[1];
    }

    std::memset(word, 0, sizeof word);
    bounded_text::Copy(word, text);
    return word;
}

// Retail formats the cooked name into 64 bytes and copies it into the 22-byte boiled name, which
// ordinary localised fish names overran into the item's other fields.
PORT_OVERRIDE int CGameDataUsed::Boiled() {
    static constexpr std::array<BoiledName, 7> basefish{
        {
         {.prefix = " ", .named = false},
         {.prefix = "Grilled ", .named = true},
         {.prefix = "R[UNI00f4]tir ", .named = true},
         {.prefix = "Ger[UNI00f6]steter ", .named = true},
         {.prefix = "Cuoci ", .named = true},
         {.prefix = "Asar ", .named = true},
         {.prefix = "Roast ", .named = true},
         }
    };

    const auto &words = LanguageEntry(basefish);
    std::string text{words.prefix};
    if (words.named) {
        text += bounded_text::View(GetName(ITEM_NAME_TYPE_PLAIN));
    }

    // ConvertFontCode only shortens text, so the source and its result share one bound.
    std::array<char, 0x40> source{};
    std::array<char, 0x40> converted{};
    bounded_text::Copy(source, text);
    ConvertFontCode(source.data(), converted.data());
    converted.back() = '\0';

    const int value = data.fish.size / 100 + (data.fish.param[BREEDFISH_STAT_BOOST] + data.fish.param[BREEDFISH_STAT_ENDURANCE] + data.fish.param[BREEDFISH_STAT_TENACITY]) / 3;
    const int base_item_no = item_no;
    bounded_text::Copy(data.boiled.name, bounded_text::View(converted.data(), converted.size()));
    data.boiled.base_item_no = base_item_no;
    data.boiled.value = value + 0x14;
    item_type = ITEM_DATA_BOILED;
    used_type = USED_ITEM_TYPE_BOILED;
    item_no = ITEM_ID_BOILED_FISH;
    return true;
}

// Retail accepts any name under 32 bytes for every family, overrunning the 21-byte fish name and the
// 26-byte attachment name. Names are cut to the family's field instead.
PORT_OVERRIDE void CGameDataUsed::SetName(char *name) {
    const auto field = RenameField(*this);
    const auto text = bounded_text::View(name);
    if (field.empty() || name == nullptr || !bounded_text::Fits(text, 0x20)) {
        return;
    }

    bounded_text::Copy(field, text);
    const char *default_name = GetItemMessage(item_no);
    rename_flag = default_name != nullptr && std::strcmp(default_name, field.data()) != 0 ? 1 : 0;
}

// Retail quotes an attachment's 26-byte name, which a save can leave unterminated, through sprintf
// into 64 bytes; the native copy reads the name only within its field.
PORT_OVERRIDE void CGameDataUsed::GetMsgAddInfo(char **message, char **extra_message, int *values) {
    static char temp[0x40];

    if (values != nullptr) {
        values[0] = 0;
    }
    if (extra_message != nullptr) {
        *extra_message = nullptr;
    }
    *message = GetName(ITEM_NAME_TYPE_QUOTED);

    switch (used_type) {
        case USED_ITEM_TYPE_ITEM:
            if (item_no == ITEM_ID_MEDAL_HOLDER) {
                values[0] = GetUserDataMan()->GetYarikomiMedal();
            }
            break;
        case USED_ITEM_TYPE_WEAPON:
            if (values != nullptr) {
                values[0] = GetLevel();
            }
            break;
        case USED_ITEM_TYPE_ATTACH: {
            const ATTACH_USED &body = data.attach;
            if (values != nullptr) {
                values[0] = static_cast<s8>(body.spectol_type) == SPECTOL_TYPE_WEAPON ? body.level : 0;
                values[1] = body.spectol_value;
            }

            const bool                      spectrumised = static_cast<s8>(body.spectol_type) != SPECTOL_TYPE_NONE;
            std::optional<std::string_view> source;
            if (spectrumised) {
                source = bounded_text::View(body.name, sizeof body.name);
            } else {
                source = MessageName(item_no);
            }
            *message = spectrumised ? data.attach.name : GetItemMessage(item_no);
            if (source) {
                // Japanese quotes with corner brackets, other languages with double quotes.
                const auto quoted = LanguageCode == 0 ? std::format("\x81w{}\x81x", *source) : std::format("\"{}\"", *source);
                bounded_text::Copy(temp, quoted);
                *message = temp;
            }

            *extra_message = GetName(ITEM_NAME_TYPE_LEVEL);
            break;
        }
        default:
            break;
    }
}
