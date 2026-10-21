#pragma once
#include <scriptinterpreter.hpp>

// The native script guard holds these character-script tags' names to their fields by these names.
int _MODEL(SPI_STACK *stack, int count) asm("Character_Model");
int _SEQ_START(SPI_STACK *stack, int count) asm("Character_SequenceStart");
int _EFFECT(SPI_STACK *stack, int count) asm("Character_Effect");
int _OUTLINE(SPI_STACK *stack, int count) asm("Character_Outline");
int _OBJECT_NAME(SPI_STACK *stack, int count) asm("Character_ObjectName");
int _OBJECT_NAME2(SPI_STACK *stack, int count) asm("Character_ObjectName2");
int _VERTEX_ANIME(SPI_STACK *stack, int count) asm("Character_VertexAnime");
int _KEY(SPI_STACK *stack, int count) asm("Character_Key");
int _SEQ(SPI_STACK *stack, int count) asm("Character_Sequence");
int _SKIN_MODEL(SPI_STACK *stack, int count) asm("Character_SkinModel");
