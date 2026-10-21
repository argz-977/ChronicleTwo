#pragma once

#include <span>

#include "runscript.hpp"
#include "scriptinterpreter.hpp"

/**
 *
 * Checks the arguments of script handlers that index fixed tables or copy into fixed fields with values a script supplies.
 *
 */
namespace script_guards {

/**
 *
 * External function a run script calls.
 *
 */
using RunScriptFunction = int (*)(RS_STACKDATA *, int);

/**
 *
 * Whether a run-script external may be called with its arguments; a refused call is reported once per handler.
 *
 */
bool AllowExternal(RunScriptFunction function, RS_STACKDATA *args, int argc, int index);

/**
 *
 * Gives the argument count a configuration tag's handler may see, or -1 when the call is refused.
 *
 */
int TagArguments(SPI_TAG_FUNCTION function, SPI_STACK *args, int argc, const char *name);

/**
 *
 * Guards the event externals, which retail defines static, through their slots in the event table.
 *
 */
void GuardEventExternals(std::span<RunScriptFunction> table);

} // namespace script_guards
