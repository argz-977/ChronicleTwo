#pragma once
#include <gamedata.hpp>
#include <scriptinterpreter.hpp>

class mgCMemory;

// Share retail dispatch and name allocation with the native message arena.
extern SPI_TAG_PARAM gamedata_tag[25] asm("GameData_Tags");
extern mgCMemory    *gamedata_build_stack asm("GameData_BuildStack");


// The native item-table tags keep their cursors inside the retail tables through these.
extern CDataCommon *comdatapt asm("GameData_CommonCursor");
extern int          comdatapt_num asm("GameData_CommonCount");
extern CDataCommon  local_com_itemdata[ITEM_COMMON_DATA_MAX] asm("GameData_CommonItems");
extern CDataWeapon  local_weapondata[116] asm("GameData_Weapons");
extern short        local_itemdatano_converttable[ITEM_NO_MAX] asm("GameData_ItemIndex");
extern CDataItem      local_itemdata[162] asm("GameData_Items");
extern CDataAttach    local_attachdata[38] asm("GameData_Attachments");
extern CDataRoboPart  local_robodata[68] asm("GameData_RoboParts");
extern CDataBreedFish local_fishdata[20] asm("GameData_Fish");
extern short          local_guarddata[35] asm("GameData_Guards");
