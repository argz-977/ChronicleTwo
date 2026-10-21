#include "common.h"

#include <cstddef>

#include "nd_meswin.hpp"

namespace {

/**
 *
 * Bytes of the message buffer ClsMes::MakeMesWin passes, which ClsMes::NeedMesWinWH copies.
 *
 */
constexpr std::size_t kMessageCapacity = 0x200;

} // namespace

// Retail stops after 512 bytes without a terminator, which NeedMesWinWH's strcpy then ran past, and its
// '@' branch wrote a second terminator one byte beyond the buffer. Text is held to 510 bytes so both
// terminators always fit.
PORT_OVERRIDE void PreMesMake(char *source, char *buffer) {
    constexpr std::size_t limit = kMessageCapacity - 2;
    const char           *src = source;
    std::size_t           length = 0;

    while (length < limit && *src != '\0') {
        const char c = *src;
        if (c == '\\' && src[1] == 'n') {
            src += 2;
            buffer[length++] = '\n';
        } else if (c == '\n') {
            if (src[1] == '@') {
                break;
            }
            ++src;
            buffer[length++] = '\n';
        } else if (c == '\r' && src[1] == '\n') {
            if (src[2] == '@') {
                break;
            }
            src += 2;
            buffer[length++] = '\n';
        } else {
            buffer[length++] = c;
            ++src;
        }
    }

    buffer[length] = '\0';
    buffer[length + 1] = '\0';
}
