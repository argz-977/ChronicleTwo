#include "common.h"

#include <array>
#include <string>

#include "dataread.hpp"
#include "event_func.hpp"
#include "menucommon.hpp"

namespace {

/**
 * Gives the background read slot of a file named relative to the current directory.
 */
BG_READ_INFO *ReadSlotInCurrentDir(const char *name) {
    std::array<char, 0x80> directory{};
    GetCurrentDir(directory.data());
    std::string path = directory.data();
    if (name != nullptr) {
        path += name;
    }
    return GetReadBGFile(path.data());
}

} // namespace

// Retail appends a script's file name to the current directory in a 128-byte buffer without a bound.
PORT_OVERRIDE unsigned int *CheckLoadedBGFile(char *name, int *size) {
    BG_READ_INFO *info = ReadSlotInCurrentDir(name);
    if (info == nullptr || info->busy == 0) {
        return nullptr;
    }
    *size = info->size;
    return reinterpret_cast<unsigned int *>(info->buffer);
}

PORT_OVERRIDE BG_READ_INFO *GetReadBGInfo(char *name) {
    return ReadSlotInCurrentDir(name);
}
