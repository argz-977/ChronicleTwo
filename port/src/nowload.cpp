#include "common.h"

#include "nowload.hpp"
#include "nowload_port.hpp"
#include "platform/config.hpp"
#include "test_hooks.hpp"

PORT_OVERRIDE void NowLoadingBarSteEnd() {
    PortProgBarCnt = PortLoadInfo.step_count;
    PortProgBarWidthStep = GameFastLoad() ? 1.0f : 0.05f;
}

bool PortSkipIntro() { return ConfigGet().skip_intro; }
