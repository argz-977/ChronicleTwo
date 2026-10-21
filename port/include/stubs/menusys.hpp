#pragma once
#include <menusys.hpp>
extern CItemSelect     *ItemSelectPtr asm("MenuMouse_ItemSelect");
extern ITEMCMD_RET_PARA MenuItemCmdRet asm("QuickLoadout_ItemCmdRet");
void                    PortRetailMenuItemSelectInit(mgCMemory *stack, int *tex_block, int mode) asm("MenuSys_MenuItemSelectInit");
void                    PortRetailMenuItemSelectDraw() asm("MenuSys_MenuItemSelectDraw");
void                    PortRetailMenuItemDraw() asm("MenuSys_MenuItemDraw");
int                     PortRetailMenuItemInit(mgCMemory *stack, int *tex_block, int mode) asm("MenuSys_MenuItemInit");
