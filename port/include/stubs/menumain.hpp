#pragma once
#include <menumain.hpp>
extern CMenuInter *CMenuInterPt asm("MenuMouse_Inter");
int                PortRetailMenuMainInit(MENU_INIT_ARG *) asm("MenuMain_MenuMainInit");
int                PortRetailMenuMainExit() asm("MenuMain_MenuMainExit");
void               PortRetailMenuWorldTrans() asm("MenuMain_MenuWorldTrans");
