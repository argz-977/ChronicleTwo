#include "common.h"

#include "mg_texture.hpp"
#include "mglib.hpp"
#include "nd_meswin.hpp"
#include "nowload_port.hpp"
#include "title.hpp"

extern ClsMes *TitleMCCheckMes asm("Title_MCCheckMes");
int            PortRetailTitleCopyRightStep() asm("Title_TitleCopyRightStep");

namespace {
// System message 102, "Reading memory card (PS2).", which retail shows while it scans both cards at boot.
constexpr int kReadingCardMessage = 0x66;
} // namespace

// The PC save folder needs no boot scan notice. The no-space and no-card notices still draw.
PORT_OVERRIDE void TitleMCCheckDraw() {
    if (TitleMCCheckMes != nullptr && TitleMCCheckMes->mes_no != kReadingCardMessage) {
        mgTexManager.ReloadTexture(0x46, static_cast<sceVif1Packet *>(nullptr));
        TitleMCCheckMes->Step();
        TitleMCCheckMes->DrawMesWin();
    }
}

// The copyright notice and the Level-5 logo movie, which game.skip_intro passes straight by.
PORT_OVERRIDE int TitleCopyRightStep() {
    return PortSkipIntro() ? 1 : PortRetailTitleCopyRightStep();
}
