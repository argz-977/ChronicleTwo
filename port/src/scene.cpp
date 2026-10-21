#include "common.h"

#include "mg_memory.hpp"
#include "scene.hpp"
#include "scenesnd.hpp"

// Retail walks stack[] at a 4-byte stride ((u8 *) this + 8 + i * 4); indexing keeps the host pointer size.
PORT_OVERRIDE void CScene::ClearStack(int index) {
    for (int i = index; i < stack_num; i++) {
        mgCMemory *memory = stack[i];

        if (memory == nullptr) {
            continue;
        }

        memory->stack_used = 0;
        memory->lock = 0;

        if (index < i) {
            memory->stSetBuffer(nullptr, 0);
        }
    }
}
