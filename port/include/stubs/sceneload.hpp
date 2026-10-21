#pragma once

#include <sceneload.hpp>

// LoadMapData reads a map's files into this buffer, sized to them (port/src/sceneload.cpp), in place of info.load_buf.
u8 *PortMapLoadBuffer(const SCN_LOADMAP_INFO2 &info);
