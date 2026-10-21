#pragma once

#include <mg_dataset.hpp>

// Retail frame copy, preserved for the native seam that also copies transform conventions.
void PortRetailCopyFrame(mgCFrame *, mgCFrame *, mgCMemory *, int, mgCFrame **) asm("MgDataset_CopyFrame");
