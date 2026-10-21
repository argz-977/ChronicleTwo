#pragma once

class CMenuPosDataForm;
struct MENUFORMPARTS_TYPE;
struct MENU_PARTS_EFFECT_STRUCT1;
struct MENU_FORM_ACTION;

/** Native menu layouts and command scripts keep the layout script's cursors and current entries between calls. */
extern unsigned short             MenuTexPosNo asm("MenuCommon_TexPosNo");
extern unsigned short             MenuTexPosNo_local asm("MenuCommon_TexPosNoLocal");
extern short                      menu_analyze_texblock asm("MenuCommon_AnalyzeTexBlock");
extern short                      Menu_Target_No asm("MenuCommon_TargetNo");
extern short                      Menu_Target_No_local asm("MenuCommon_TargetNoLocal");
extern CMenuPosDataForm          *menu_formPt asm("MenuCommon_FormPt");
extern MENUFORMPARTS_TYPE        *menu_form_part asm("MenuCommon_FormPart");
extern MENU_PARTS_EFFECT_STRUCT1 *menu_parts_effect_ptr asm("MenuCommon_PartsEffectPtr");
extern short                      menu_analyze_formno asm("MenuCommon_AnalyzeFormNo");
extern short                      menu_analyze_formno_offset asm("MenuCommon_AnalyzeFormNoOffset");
extern int                        menu_form_partsno asm("MenuCommon_FormPartsNo");
extern MENU_FORM_ACTION          *menu_spi_form_action_info asm("MenuCommon_FormActionInfo");
extern unsigned char              SpiMenuExeCommandFlag asm("MenuCommon_ExeCommandFlag");
extern char                       MenuSpiTextureName[0x20] asm("MenuCommon_SpiTextureName");
