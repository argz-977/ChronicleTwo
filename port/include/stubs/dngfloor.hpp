#pragma once
#include <scriptinterpreter.hpp>

class CDngFloorManager;
class mgCMemory;

// The native script guard limits the room-link tag to the room's four directions by this name.
int _ROOM_LINK(SPI_STACK *stack, int count) asm("DngFloor_RoomLink");

/** The floor title text script still runs through retail's tag table, which reads these. */
extern SPI_TAG_PARAM     tree_map_tag[12] asm("DngFloor_TreeMapTag");
extern CDngFloorManager *tree_dngmap asm("DngFloor_TreeMap");
extern mgCMemory        *tree_spi_stack asm("DngFloor_TreeSpiStack");

/** Native map loading picks a negative room picture from these group bases, as RI_TEX does. */
extern int offsetTable_911[5] asm("DngFloor_TextureGroupBase");
