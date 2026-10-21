#pragma once

#include <scriptinterpreter.hpp>

class mgCMemory;

/** Native prize loading shares the original prize callbacks and allocation state. */
extern SPI_TAG_PARAM gyoprize_tag[4] asm("Aqua_FishPrizeTags");
extern mgCMemory    *fish_prize_buildstack asm("Aqua_FishPrizeBuildStack");
extern signed char   FishTournamentGoodsType asm("Aqua_FishTournamentGoodsType");

// The native script guard checks the fish-race list tag's class and count by this name.
int _GYORACE_LISTNUM(SPI_STACK *stack, int arg_count) asm("Aqua_GyoRaceListNum");
