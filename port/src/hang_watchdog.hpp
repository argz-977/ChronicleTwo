#pragma once

/**
 * Marks the end of a game frame for the hang watchdog. With DC2_HANG_ABORT_SECONDS set, a game thread that ends no
 * frame for that many seconds is aborted, so the crash report records where it stopped; otherwise this does nothing.
 */
void HangWatchdogFrame();
