#pragma once
#include <runscript.hpp>

// The native script guard checks these action-script externals' table indices by these names.
int _CHECK_EQUIP(RS_STACKDATA *stack, int argc) asm("ActScript_CheckEquip");
int _SET_ACCUME_FX(RS_STACKDATA *stack, int argc) asm("ActScript_SetAccumeFx");
int _SET_SHOT(RS_STACKDATA *stack, int argc) asm("ActScript_SetShot");
int _GET_ATTK_POINT(RS_STACKDATA *stack, int argc) asm("ActScript_GetAttackPoint");
