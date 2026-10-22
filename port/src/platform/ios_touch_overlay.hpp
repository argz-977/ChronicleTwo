#pragma once

/**
 * Shows iOS's on-screen gamepad over the game's window from now on, while InputTouchOverlayVisible() holds: no
 * gamepad, keyboard or mouse connected. Its buttons and sticks drive pad 0. Called on the main thread.
 */
void IosTouchOverlayStart();
