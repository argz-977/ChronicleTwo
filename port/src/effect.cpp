#include "common.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#include "bounded_text.hpp"
#include "command_replay.hpp"
#include "effect.hpp"
#include "scriptinterpreter.hpp"
#include "stubs/effect.hpp"

namespace {

/**
 *
 * Emitter between an EFFECT_START tag and its EFFECT_END, owned by the loader.
 *
 */
std::unique_ptr<CEffectCtrl> open_emitter;

/**
 *
 * Takes the settings of tags written outside an EFFECT_START block, which retail wrote through a null emitter.
 *
 */
CEffectCtrl &DiscardEmitter() {
    static CEffectCtrl emitter;
    return emitter;
}

/**
 *
 * Gives a tag argument, inside the interpreter's argument stack.
 *
 */
SPI_STACK *Argument(SPI_STACK *args, int index) {
    return index < SPI_STACK_SIZE ? args + index : nullptr;
}

/**
 *
 * Gives an integer tag argument, or zero outside the argument stack.
 *
 */
int IntArgument(SPI_STACK *args, int index) {
    SPI_STACK *argument = Argument(args, index);
    return argument != nullptr ? spiGetStackInt(argument) : 0;
}

/**
 *
 * Gives a string tag argument as text, empty when the argument is not a string.
 *
 */
std::string_view TextArgument(SPI_STACK *args, int index) {
    SPI_STACK *argument = Argument(args, index);
    return argument != nullptr ? bounded_text::View(spiGetStackString(argument)) : std::string_view();
}

/**
 *
 * Sets the particle and emitter pools, holding the emitters to the manager's per-emitter tables.
 *
 */
int BufferSize(SPI_STACK *args, int /*unused*/) {
    const int particles = std::max(IntArgument(args, 0), 0);
    const int emitters = std::clamp(IntArgument(args, 1), 0, static_cast<int>(std::size(g_tmp_effm->wait_frame)));
    g_tmp_effm->SetEffectNums(particles, emitters);
    return 1;
}

/**
 *
 * Opens a named emitter definition, discarding one left open by a missing EFFECT_END.
 *
 */
int EffectStart(SPI_STACK *args, int /*unused*/) {
    bounded_text::Copy(g_tmp_eff_name, TextArgument(args, 0));
    open_emitter = std::make_unique<CEffectCtrl>();
    g_tmp_effc = open_emitter.get();
    return 1;
}

/**
 *
 * Enters the open emitter into the manager when loading, and closes it.
 *
 */
int EffectEnd(SPI_STACK * /*unused*/, int /*unused*/) {
    if (open_emitter != nullptr && g_eff_entry_flag != 0) {
        g_tmp_effm->EnterEffectCtrl(*open_emitter, g_tmp_eff_name);
    }
    open_emitter.reset();
    g_tmp_effc = &DiscardEmitter();
    return 1;
}

/**
 *
 * Sets the start delay of one of the manager's emitters.
 *
 */
int WaitFrame(SPI_STACK *args, int /*unused*/) {
    const int index = IntArgument(args, 0);
    if (index >= 0 && std::cmp_less(index, std::size(g_tmp_effm->wait_frame))) {
        g_tmp_effm->wait_frame[index] = IntArgument(args, 1);
    }
    return 1;
}

/**
 *
 * Names the texture archive the effect needs.
 *
 */
int ImageName(SPI_STACK *args, int /*unused*/) {
    bounded_text::Copy(g_tmp_effm->img_name, TextArgument(args, 0));
    return 1;
}

/**
 *
 * Reads the open emitter's texture rectangles, no more than it has room for.
 *
 */
int TextureRects(SPI_STACK *args, int /*unused*/) {
    auto     &rects = g_tmp_effc->tex_rect;
    const int count = std::clamp(IntArgument(args, 0), 0, static_cast<int>(std::size(rects)));
    g_tmp_effc->tex_rect_num = count;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; std::cmp_less(j, std::size(rects[i])); ++j) {
            rects[i][j] = IntArgument(args, 1 + i * static_cast<int>(std::size(rects[i])) + j);
        }
    }
    return 1;
}

/**
 *
 * Native handler that replaces a retail effect-script tag.
 *
 */
struct Replacement {
    std::string_view name;     /**< Tag the handler replaces. */
    SPI_TAG_FUNCTION function; /**< Native handler. */
};

/**
 *
 * Gives retail's effect-script tags with the native handlers in place of those that own memory or index tables.
 *
 */
SPI_TAG_PARAM *Tags() {
    static constexpr std::array<Replacement, 6> replacements{
        {
         {.name = "BUFFER_SIZE", .function = BufferSize},
         {.name = "EFFECT_START", .function = EffectStart},
         {.name = "EFFECT_END", .function = EffectEnd},
         {.name = "WAIT_FRAME", .function = WaitFrame},
         {.name = "IMG_NAME", .function = ImageName},
         {.name = "TEX_GET_RECT", .function = TextureRects},
         }
    };
    static auto table = [] {
        std::array<SPI_TAG_PARAM, std::size(effm_tag)> tags{};
        std::ranges::copy(effm_tag, tags.begin());
        for (auto &tag : tags) {
            if (tag.name == nullptr) {
                break;
            }
            const auto *const found = std::ranges::find(replacements, std::string_view(tag.name), &Replacement::name);
            if (found != replacements.end()) {
                tag.function = found->function;
            }
        }
        return tags;
    }();
    return table.data();
}

/**
 *
 * Runs an effect script against a manager, entering its emitters when entry is set.
 *
 */
void RunEffectScript(CEffectManager &manager, char *script, int size, int entry) {
    CScriptInterpreter interpreter;
    g_tmp_effm = &manager;
    g_eff_entry_flag = entry;
    g_tmp_effc = &DiscardEmitter();
    // Character effect scripts embedded in .game.json resources are typed command lists.
    if (command_replay::IsCommandBuffer(script, size)) {
        command_replay::RunBuffer(script, size, Tags());
    } else {
        interpreter.SetTag(Tags());
        interpreter.SetScript(script, size);
        interpreter.Run();
    }
    open_emitter.reset();
    g_tmp_effc = &DiscardEmitter();
}

} // namespace

// Retail allocates each emitter with new and frees it at EFFECT_END, leaking one left open, and its
// handlers index the manager's eight-entry tables and copy names without bounds.
PORT_OVERRIDE void CEffectManager::GetBufferNums(char *script, int size, int *effect_num, int *ctrl_num) {
    RunEffectScript(*this, script, size, 0);
    *effect_num = this->effect_num;
    *ctrl_num = this->ctrl_num;
}

PORT_OVERRIDE void CEffectManager::Load(char *script, int size) {
    RunEffectScript(*this, script, size, 1);
    load = 1;
}
