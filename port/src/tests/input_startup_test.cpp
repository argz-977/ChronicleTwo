#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <utility>

#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "platform/input_startup.hpp"
#include "platform/settings.hpp"

namespace {
/**
 * Supplies an isolated virtual gamepad for the game's input preferences.
 */
class InputStickSettings : public testing::Test {
protected:
    SDL_JoystickID joystick_id = 0;    /**< Virtual device identifier. */
    SDL_Joystick  *joystick = nullptr; /**< Open virtual controller. */

    void SetUp() override {
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1234/0x5678");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        InputInitForRun(false);
        SDL_VirtualJoystickDesc description{};
        SDL_INIT_INTERFACE(&description);
        description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        description.vendor_id = 0x1234;
        description.product_id = 0x5678;
        description.naxes = SDL_GAMEPAD_AXIS_COUNT;
        description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        description.name = "Camera settings test";
        joystick_id = SDL_AttachVirtualJoystick(&description);
        ASSERT_NE(joystick_id, 0u) << SDL_GetError();
        joystick = SDL_OpenJoystick(joystick_id);
        ASSERT_NE(joystick, nullptr) << SDL_GetError();
    }

    void TearDown() override {
        SDL_CloseJoystick(joystick);
        SDL_DetachVirtualJoystick(joystick_id);
        InputShutdown();
        SDL_ResetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT);
        SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }
};
} // namespace

TEST_F(InputStickSettings, DefaultsReverseRightStickWhileLeftStickStaysUninverted) {
    for (const auto axis : {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY,
                            SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY}) {
        ASSERT_TRUE(SDL_SetJoystickVirtualAxis(joystick, axis, 20000));
    }
    InputPoll();
    EXPECT_LT(InputGetPad(0).right_x, kInputAxisCentre);
    EXPECT_LT(InputGetPad(0).right_y, kInputAxisCentre);
    EXPECT_GT(InputGetPad(0).left_x, kInputAxisCentre);
    EXPECT_GT(InputGetPad(0).left_y, kInputAxisCentre);

    for (bool invert_x : {false, true}) {
        for (bool invert_y : {false, true}) {
            HostSettings settings;
            settings.stick_invert_x = invert_x;
            settings.stick_invert_y = invert_y;
            InputApplyGameConfig(settings);
            InputPoll();
            EXPECT_EQ(InputGetPad(0).right_x > kInputAxisCentre, invert_x);
            EXPECT_EQ(InputGetPad(0).right_y > kInputAxisCentre, invert_y);
            EXPECT_GT(InputGetPad(0).left_x, kInputAxisCentre);
            EXPECT_GT(InputGetPad(0).left_y, kInputAxisCentre);
            InputSetLookOnLeftStick(true);
            InputPoll();
            EXPECT_GT(InputGetPad(0).left_x, kInputAxisCentre);
            EXPECT_GT(InputGetPad(0).left_y, kInputAxisCentre);
            InputSetLookOnLeftStick(false);
        }
    }
}

TEST(InputStartup, HeadlessLeavesControllerSubsystemsOff) {
    ASSERT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), 0u);
    InputInitForRun(true);
    EXPECT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), 0u);
    InputShutdown();
}

TEST(InputStartup, HeadlessKeepsScriptedFirstPad) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 cross start 255 128 128 128\n", script, error)) << error;
    InputInitForRun(true);
    InputScriptInstall(std::move(script));
    InputPoll();
    InputScriptApply(0);
    const InputPadState &actual = InputGetPad(0);
    EXPECT_TRUE(actual.connected);
    EXPECT_EQ(actual.buttons, kInputCross | kInputStart);
    EXPECT_EQ(actual.left_x, 255);
    InputScriptInstall({});
    InputShutdown();
}

TEST(InputStartup, HeadlessKeepsScriptedKeyboard) {
    InputScript script;
    std::string error;
    ASSERT_TRUE(InputScriptParse("0 key:Space\n", script, error)) << error;
    InputInitForRun(true);
    InputScriptInstall(std::move(script));
    InputPoll();
    InputScriptApply(0);
    EXPECT_NE(InputGetPad(0).buttons & kInputCross, 0u);
    InputScriptInstall({});
    InputShutdown();
}

TEST(InputStartup, WindowedStillInitializesControllers) {
    InputInitForRun(false);
    EXPECT_EQ(SDL_WasInit(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK), SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    InputShutdown();
}
