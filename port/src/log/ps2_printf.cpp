#include "ps2_printf.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#include "log/log_internal.hpp"

#undef printf

int CtPs2Printf(const char *format, ...) {
    char    stack[1024];
    va_list args{};
    va_start(args, format);
    va_list again{};
    va_copy(again, args);
    const int size = std::vsnprintf(stack, sizeof stack, format, args);
    va_end(args);
    if (size < 0) {
        va_end(again);
        return size;
    }
    std::string      heap;
    std::string_view text(stack, static_cast<std::size_t>(size));
    if (static_cast<std::size_t>(size) >= sizeof stack) {
        heap.resize(static_cast<std::size_t>(size));
        std::vsnprintf(heap.data(), heap.size() + 1, format, again);
        text = heap;
    }
    va_end(again);
    if (LogConsoleEnabled()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
    }
    Ps2PrintfWrite(text);
    return size;
}
