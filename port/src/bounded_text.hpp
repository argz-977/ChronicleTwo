#pragma once

#include <cstddef>
#include <span>
#include <string_view>

/**
 *
 * Bounded reads and writes of the game's fixed-size, NUL-terminated text fields.
 *
 */
namespace bounded_text {

/**
 *
 * Whether a byte opens a two-byte Shift-JIS character.
 *
 */
constexpr bool IsShiftJisLead(unsigned char byte) {
    return (byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC);
}

/**
 *
 * Views a field's text up to its terminator or the end of the field, whichever comes first.
 *
 */
constexpr std::string_view View(const char *text, std::size_t capacity) {
    if (text == nullptr) {
        return {};
    }
    std::size_t length = 0;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return {text, length};
}

/**
 *
 * Views a terminated string, treating a null pointer as empty text.
 *
 */
constexpr std::string_view View(const char *text) {
    return text != nullptr ? std::string_view(text) : std::string_view();
}

/**
 *
 * Gives the longest prefix of the text that fits in limit bytes without splitting a Shift-JIS character.
 *
 */
constexpr std::string_view Fit(std::string_view text, std::size_t limit) {
    std::size_t length = 0;
    while (length < text.size()) {
        const std::size_t width = IsShiftJisLead(static_cast<unsigned char>(text[length])) && length + 1 < text.size() ? 2 : 1;
        if (length + width > limit) {
            break;
        }
        length += width;
    }
    return text.substr(0, length);
}

/**
 *
 * Copies text into a field, truncating it so the terminator always fits, and returns the bytes copied.
 *
 */
inline std::size_t Copy(std::span<char> field, std::string_view text) {
    if (field.empty()) {
        return 0;
    }
    const auto fitted = Fit(text, field.size() - 1);
    fitted.copy(field.data(), fitted.size());
    field[fitted.size()] = '\0';
    return fitted.size();
}

/**
 *
 * Whether the text and its terminator fit in a field of the given capacity.
 *
 */
constexpr bool Fits(std::string_view text, std::size_t capacity) {
    return text.size() < capacity;
}

} // namespace bounded_text
