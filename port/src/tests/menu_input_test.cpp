#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <utility>

#include "gamepad.hpp"
#include "mainloop.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "platform/clock.hpp"
#include "platform/input.hpp"
#include "platform/input_script.hpp"

namespace {

class MenuInput : public testing::Test {
protected:
    void SetUp() override {
        InputResetBindings();
        GamePad__2.MenuModeOff();
    }

    void TearDown() override {
        GamePad__2.MenuModeOff();
        GamePad__2.AutoRepeatOff();
        InputScriptInstall({});
        InputResetBindings();
    }

    static unsigned Buttons(int scancode) {
        InputKeyboardMouse held;
        held.keys.push_back(scancode);
        return InputApplyKeyboardMouse({}, held).buttons;
    }
};

TEST_F(MenuInput, EscapeTracksRetailMenuMode) {
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    GamePad__2.MenuModeOn(120);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    GamePad__2.MenuModeOff();
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
}

TEST_F(MenuInput, MenuKeyEnvironmentPropagatesEveryLayout) {
    SetMenuKeyCtrlEnv(0);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    SetMenuKeyCtrlEnv(1);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    SetMenuKeyCtrlEnv(2);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 120);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
    SetMenuKeyCtrlEnv(3);
    EXPECT_EQ(GamePad__2.axis_threshold[0], 0);
    EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
}

TEST_F(MenuInput, MenuModePreservesThresholdAndSecondPort) {
    CGamePad pad{};
    pad.axis_threshold[1] = 37;
    for (int threshold : {0, -1, 64, 120}) {
        pad.MenuModeOn(threshold);
        EXPECT_EQ(pad.axis_threshold[0], threshold);
        EXPECT_EQ(pad.axis_threshold[1], 37);
        EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputCircle);
        pad.MenuModeOff();
        EXPECT_EQ(pad.axis_threshold[0], 0);
        EXPECT_EQ(pad.axis_threshold[1], 37);
        EXPECT_EQ(Buttons(SDL_SCANCODE_ESCAPE), kInputTriangle);
    }
}

TEST_F(MenuInput, OtherDefaultKeysRetainTheirPadButtons) {
    for (bool menu : {false, true}) {
        if (menu) {
            GamePad__2.MenuModeOn(120);
        }
        EXPECT_EQ(Buttons(SDL_SCANCODE_TAB), kInputTriangle);
        EXPECT_EQ(Buttons(SDL_SCANCODE_RETURN), kInputStart);
        EXPECT_EQ(Buttons(SDL_SCANCODE_E), kInputSquare);
        EXPECT_EQ(Buttons(SDL_SCANCODE_SPACE), kInputCross);
    }
}

TEST_F(MenuInput, EnglishMenuReadsEscapeAsCancelAndSpaceAsConfirm) {
    const int language = LanguageCode;
    LanguageCode = LANG_ENGLISH;
    GamePad__2.key_lock = 0;
    GamePad__2.previous_pad[0].button = 0;
    GamePad__2.MenuModeOn(120);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_ESCAPE);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_CANCEL);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_SPACE);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_DECIDE);
    GamePad__2.pad[0].button = Buttons(SDL_SCANCODE_TAB);
    EXPECT_EQ(MenuCheckPushButton(), MENU_PUSH_BUTTON_TRIANGLE);
    EXPECT_EQ(ConvertCheckPushButton(MenuCheckPushButton()), MENU_PUSH_BUTTON_CANCEL);
    GamePad__2.pad[0].button = 0;
    LanguageCode = language;
}

TEST_F(MenuInput, RetailUpdateConvertsLeftStickOnlyPastMenuThreshold) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 pad1 255 128 128 128\n5 pad1 0 128 128 128\n6 pad1 128 255 128 128\n7 pad1 128 0 128 128\n8 pad1 255 128 128 128\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    constexpr int directions = PAD_RIGHT | PAD_LEFT | PAD_DOWN | PAD_UP;
    GamePad__2.MenuModeOn(120);
    int frame = 4;
    for (int direction : {PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP}) {
        InputScriptApply(frame++);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.pad[0].button & directions, direction);
    }
    GamePad__2.MenuModeOff();
    InputScriptApply(8);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.GetLX(), 128);
    EXPECT_EQ(GamePad__2.pad[0].button & directions, 0);
    GamePad__2.MenuModeOn(128);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[0].button & directions, 0);
    GamePad__2.MenuModeOn(127);
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[0].button & directions, PAD_RIGHT);
}

TEST_F(MenuInput, KeyboardStickNavigatesOnlyInRetailMenuMode) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 key:W\n5 key:S\n6 key:A\n7 key:D\n8 key:Up\n9 key:Down\n10 key:Left\n11 key:Right\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    constexpr int directions = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    for (bool menu : {false, true}) {
        GamePad__2.Init();
        for (int frame = 0; frame < 4; ++frame) {
            InputScriptApply(frame);
            GamePad__2.UpDate();
        }
        if (menu) {
            GamePad__2.MenuModeOn(120);
        }
        for (int frame = 4; frame < 8; ++frame) {
            InputScriptApply(frame);
            GamePad__2.UpDate();
            const int expected[] = {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT};
            EXPECT_EQ(GamePad__2.pad[0].button & directions, menu ? expected[frame - 4] : 0)
                << "menu=" << menu << " frame=" << frame;
            EXPECT_NE(frame < 6 ? GamePad__2.GetLY() : GamePad__2.GetLX(), 0);
        }
        int frame = 8;
        for (int direction : {PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT}) {
            InputScriptApply(frame++);
            GamePad__2.UpDate();
            EXPECT_EQ(GamePad__2.pad[0].button & directions, direction);
        }
    }
}

TEST_F(MenuInput, ReboundStickUsesThresholdAndStopsConvertingOnMenuExit) {
    const std::string_view binding[] = {"H"};
    ASSERT_TRUE(InputBindKeys("lx+", binding));
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 key:H\n5 key:Right\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    InputScriptApply(4);
    constexpr int directions = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    for (int threshold : {0, 128, 127, 0, 120, -1}) {
        GamePad__2.MenuModeOn(threshold);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.GetLX(), 128);
        EXPECT_EQ(GamePad__2.pad[0].button & directions,
                  threshold > 0 && threshold < 128 ? PAD_RIGHT : 0);
    }
    GamePad__2.MenuModeOff();
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[0].button & directions, 0);
    InputScriptApply(5);
    GamePad__2.UpDate();
    EXPECT_TRUE(GamePad__2.Down(PAD_RIGHT));
}

TEST_F(MenuInput, MixedSourcesUseRetailThresholdAndOppositeCancellation) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 key:W 255 128 128 128\n5 key:A 128 255 128 128\n6 key:Right key:A\n7 key:W key:D\n8 pad1 128 0 128 128\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    GamePad__2.MenuModeOn(120);
    constexpr int directions = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    int           frame = 4;
    for (int expected : std::initializer_list<int>{PAD_RIGHT | PAD_UP, PAD_DOWN | PAD_LEFT, 0, 0, PAD_UP}) {
        InputScriptApply(frame++);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.pad[0].button & directions, expected);
    }
}

TEST_F(MenuInput, FieldMovementKeysDoNotSuppressDpadRepeatOrSecondPadLock) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 key:A key:Right\n4 pad2 left\n12\n12 pad2\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    GamePad__2.MenuModeOff();
    GamePad__2.SetAutoRepeat(PAD_RIGHT, 3, 2);
    for (int frame = 4; frame < 12; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.GetLX(), -128);
        EXPECT_FALSE(GamePad__2.On(PAD_LEFT));
        EXPECT_EQ(GamePad__2.Down(PAD_RIGHT) != 0, frame == 4 || frame == 7 || frame == 9 || frame == 11);
        EXPECT_TRUE(GamePad__2.On2(PAD_LEFT));
    }
    GamePad__2.key_lock2 = 1;
    GamePad__2.UpDate();
    EXPECT_EQ(GamePad__2.pad[1].button, 0);
    EXPECT_EQ(GamePad__2.previous_pad[1].button, 0);
    GamePad__2.key_lock2 = 0;
    InputScriptApply(12);
    GamePad__2.UpDate();
    EXPECT_FALSE(GamePad__2.On(PAD_RIGHT));
    EXPECT_EQ(GamePad__2.GetLX(), 0);
}

} // namespace

TEST_F(MenuInput, MouseMotionCannotReachMenuSticksOrRetailDirectionConversion) {
    GamePad__2.MenuModeOn(120);
    constexpr int          directions = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    const std::string_view x[] = {"MouseX"};
    const std::string_view y[] = {"MouseY"};
    ASSERT_TRUE(InputBindKeys("lx", x));
    ASSERT_TRUE(InputBindKeys("ly", y));
    for (float motion : {-10000.0f, 10000.0f}) {
        const auto pad = InputApplyKeyboardMouse({}, {.mouse_dx = motion, .mouse_dy = motion});
        EXPECT_EQ(pad.left_x, 128);
        EXPECT_EQ(pad.left_y, 128);
        EXPECT_EQ(pad.right_x, 128);
        EXPECT_EQ(pad.right_y, 128);
        EXPECT_EQ(pad.buttons & directions, 0);
    }
}

TEST_F(MenuInput, ScriptedMenuMotionLeavesRetailAxesDirectionsAndCameraNeutral) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 pad1\n0 pad2\n4 mouse:10000,10000\n5 mouse:-10000,-10000\n", script, error)) << error;
    InputScriptInstall(std::move(script));
    ClockSetUnbounded(true);
    ClockReset();
    GamePad__2.Init();
    for (int frame = 0; frame < 4; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
    }
    GamePad__2.MenuModeOn(120);
    for (int frame = 4; frame < 6; ++frame) {
        InputScriptApply(frame);
        GamePad__2.UpDate();
        EXPECT_EQ(GamePad__2.GetLX(), 0);
        EXPECT_EQ(GamePad__2.GetLY(), 0);
        EXPECT_EQ(GamePad__2.GetRX(), 0);
        EXPECT_EQ(GamePad__2.GetRY(), 0);
        EXPECT_EQ(GamePad__2.pad[0].button & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT), 0);
        EXPECT_FLOAT_EQ(InputGetMouseLook().yaw, 0);
        EXPECT_FLOAT_EQ(InputGetMouseLook().pitch, 0);
    }
}
