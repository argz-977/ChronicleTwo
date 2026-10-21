#pragma once
#include <scriptinterpreter.hpp>

// The native script guard holds the sky tags' names to their 32-byte fields by these names.
s32 _SKY_IMG(SPI_STACK *stack, s32 arg_count) asm("MapSky_SkyImage");
s32 _SKY_MDS(SPI_STACK *stack, s32 arg_count) asm("MapSky_SkyModel");
s32 _SUN_MDS(SPI_STACK *stack, s32 arg_count) asm("MapSky_SunModel");
s32 _SKYB_MDS(SPI_STACK *stack, s32 arg_count) asm("MapSky_BackSkyModel");
int _SKY_BG(SPI_STACK *stack, int argument_count) asm("MapSky_Background");
int _SKY_ANIME(SPI_STACK *stack, int argument_count) asm("MapSky_SkyAnime");
int _SKYB_ANIME(SPI_STACK *stack, int argument_count) asm("MapSky_BackSkyAnime");

struct MAP_SKY_INFO;

/** Fills a sky's settings from its native sky document, in place of the sky script's run. */
void PortLoadSkyInfo(MAP_SKY_INFO *info, char *script, int size);
