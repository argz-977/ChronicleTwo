#pragma once
#include <effect.hpp>
#include <scriptinterpreter.hpp>

// The native effect-script loader owns the open emitter and reuses retail's setters through these.
extern CEffectManager *g_tmp_effm asm("Effect_ScriptManager");
extern CEffectCtrl    *g_tmp_effc asm("Effect_ScriptEmitter");
extern int             g_eff_entry_flag asm("Effect_ScriptEntryFlag");
extern char            g_tmp_eff_name[0x20] asm("Effect_ScriptEmitterName");
extern SPI_TAG_PARAM   effm_tag[48] asm("Effect_ScriptTags");
