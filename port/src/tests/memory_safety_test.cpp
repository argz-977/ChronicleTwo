#include "common.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "bounded_text.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "runscript.hpp"
#include "script_guards.hpp"
#include "scriptinterpreter.hpp"
#include "userdata.hpp"

int _GET_VALUE(RS_STACKDATA *stack, int argument_count);

namespace {

/**
 * Argument counts and first values a binary tag handler observed.
 */
std::vector<int> observed_counts;

int CaptureCount(SPI_STACK * /*unused*/, int count) {
    observed_counts.push_back(count);
    return 1;
}

/**
 * Appends a little-endian value to a binary script.
 */
template <typename Value>
void Put(std::vector<char> &script, Value value) {
    const auto *bytes = reinterpret_cast<const char *>(&value);
    script.insert(script.end(), bytes, bytes + sizeof value);
}

/**
 * Runs a binary script against a one-tag table that records argument counts.
 */
void RunBinary(std::vector<char> script) {
    char          name[] = "TAG";
    SPI_TAG_PARAM tags[]{
        {.name = name,    .function = CaptureCount},
        {.name = nullptr, .function = nullptr     }
    };
    CScriptInterpreter interpreter;
    interpreter.SetTag(tags);
    interpreter.SetScript(script.data(), static_cast<int>(script.size()));
    interpreter.Run();
}

/**
 * Restores the language after a test changes it.
 */
struct LanguageScope {
    int previous = LanguageCode;

    explicit LanguageScope(int language) { LanguageCode = language; }

    ~LanguageScope() { LanguageCode = previous; }
};

} // namespace

TEST(BoundedText, CopyTruncatesWithoutSplittingShiftJis) {
    std::array<char, 4> field{'x', 'x', 'x', 'x'};
    EXPECT_EQ(bounded_text::Copy(field, "ab\x81w"), 2u);
    EXPECT_STREQ(field.data(), "ab");

    EXPECT_EQ(bounded_text::Copy(field, "a\x81wb"), 3u);
    EXPECT_EQ(std::string_view(field.data()), "a\x81w");
}

TEST(BoundedText, ViewStopsAtTheFieldEnd) {
    const char unterminated[3] = {'a', 'b', 'c'};
    EXPECT_EQ(bounded_text::View(unterminated, sizeof unterminated), "abc");
    EXPECT_TRUE(bounded_text::View(nullptr).empty());
}

TEST(BinaryScript, OversizedArgumentCountStaysInsideTheStack) {
    std::vector<char> script{'B', 'I', 'N', '\0'};
    Put<s16>(script, 0);
    Put<s16>(script, 300);
    script.insert(script.end(), 300, static_cast<char>(SPI_BINARY_ARG_TYPE_INT));
    for (int i = 0; i < 300; ++i) {
        Put<int>(script, i);
    }

    observed_counts.clear();
    RunBinary(script);
    ASSERT_EQ(observed_counts.size(), 1u);
    EXPECT_EQ(observed_counts[0], SPI_STACK_SIZE);
}

TEST(BinaryScript, TruncatedValuesStopAtTheScriptEnd) {
    std::vector<char> script{'B', 'I', 'N', '\0'};
    Put<s16>(script, 0);
    Put<s16>(script, 3);
    script.push_back(static_cast<char>(SPI_BINARY_ARG_TYPE_INT));
    script.push_back(static_cast<char>(SPI_BINARY_ARG_TYPE_STRING));
    script.push_back(static_cast<char>(SPI_BINARY_ARG_TYPE_INT));
    script.push_back('\0');
    Put<int>(script, 7);
    // An unterminated string ends the script.
    script.insert(script.end(), {'a', 'b', 'c'});

    observed_counts.clear();
    RunBinary(script);
    ASSERT_EQ(observed_counts.size(), 1u);
    EXPECT_EQ(observed_counts[0], 1);
}

TEST(ItemNames, RenamedWeaponNameIsNotAFormat) {
    LanguageScope language(1);
    CGameDataUsed item;
    item.used_type = USED_ITEM_TYPE_WEAPON;
    item.rename_flag = 1;
    item.data.weapon.level = 3;
    bounded_text::Copy(item.data.weapon.name, "%s%n%99999d");

    EXPECT_STREQ(item.GetName(ITEM_NAME_TYPE_LEVEL), "%s%n%99999d + 3");
}

TEST(ItemNames, FishRenameIsHeldToTheFishField) {
    CGameDataUsed item;
    item.used_type = USED_ITEM_TYPE_FISH;
    item.item_no = 0;
    std::memset(item.data.fish.name, 0, sizeof item.data.fish.name);
    item.data.fish.sex = 5;

    char name[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234";
    item.SetName(name);
    EXPECT_EQ(std::strlen(item.data.fish.name), sizeof item.data.fish.name - 1);
    EXPECT_EQ(item.data.fish.sex, 5);
}

TEST(ItemNames, BoiledNameIsHeldToTheBoiledField) {
    LanguageScope language(3);
    CGameDataUsed item;
    item.used_type = USED_ITEM_TYPE_FISH;
    item.item_no = 0;
    std::memset(&item.data, 0, sizeof item.data);
    bounded_text::Copy(item.data.fish.name, "ABCDEFGHIJKLMNOPQRST");

    item.Boiled();
    EXPECT_EQ(item.used_type, USED_ITEM_TYPE_BOILED);
    EXPECT_LT(std::strlen(item.data.boiled.name), sizeof item.data.boiled.name);
}

TEST(ScriptGuards, RefusesEffectValueOutsideTheTable) {
    std::array<RS_STACKDATA, 2> args{};
    args[0].type = RS_INT;
    args[0].val.i = 8;
    EXPECT_FALSE(script_guards::AllowExternal(_GET_VALUE, args.data(), 2, 0));
    args[0].val.i = 7;
    EXPECT_TRUE(script_guards::AllowExternal(_GET_VALUE, args.data(), 2, 0));
}
