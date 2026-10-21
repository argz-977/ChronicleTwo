#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "mg_visual.hpp"
#include "password.hpp"

namespace {

constexpr u64 kTex0 = 0x0123456789ABCDEFull;
constexpr u64 kTex1 = 0xFEDCBA9876543210ull;
constexpr u64 kTexa = 0x8000000080000080ull;
constexpr u64 kHigh32 = 0xFFFFFFFFull;
constexpr int kKeySize = 20;

// The retail alphabet is file-local, so the tamper test repeats it to pick valid replacement characters.
constexpr char kAlphabet[] = "0123456789abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";

constexpr int kSizes[] = {16, 64, 256};

enum class Pattern {
    kRamp,
    kOnes,
    kHighGroups,
    kRandom
};
constexpr Pattern kPatterns[] = {Pattern::kRamp, Pattern::kOnes, Pattern::kHighGroups, Pattern::kRandom};

u64 ReadWord(const u_int *packet, int index) {
    u64 value = 0;
    std::memcpy(&value, packet + index, sizeof value);
    return value;
}

u64 GroupValue(const std::vector<u8> &bytes, int group) {
    u64 value = 0;
    std::memcpy(&value, bytes.data() + group * 8, sizeof value);
    return value;
}

std::vector<u8> MakePayload(Pattern pattern, int size, u32 seed) {
    std::vector<u8> bytes(static_cast<std::size_t>(size));
    u32             state = seed;

    for (int i = 0; i < size; i++) {
        state = state * 1664525u + 1013904223u;
        const u8 random = static_cast<u8>(state >> 24);
        u8       value = 0;

        switch (pattern) {
            case Pattern::kRamp:
                value = static_cast<u8>(i);
                break;
            case Pattern::kOnes:
                value = 0xFF;
                break;
            case Pattern::kHighGroups:
                value = (i % 8 >= 4) ? static_cast<u8>(random | 0x80) : random;
                break;
            case Pattern::kRandom:
                value = random;
                break;
        }

        bytes[static_cast<std::size_t>(i)] = value;
    }

    return bytes;
}

int TextLength(int size) {
    return size / 8 * 11;
}

class Long64 : public testing::Test {};

} // namespace

TEST_F(Long64, MgSetPkTEX0TwoRegisterFormWritesFull64BitWords) {
    alignas(16) u_int packet[24];
    std::memset(packet, 0, sizeof packet);

    ASSERT_EQ(mgSetPkTEX0(packet, kTex0, kTex1), 4);

    EXPECT_EQ(ReadWord(packet, 8), kTex1);
    EXPECT_EQ(ReadWord(packet, 10), 0x14ull);
    EXPECT_EQ(ReadWord(packet, 12), kTex0);
    EXPECT_EQ(ReadWord(packet, 14), 6ull);
    for (int i = 16; i < 24; i++) {
        EXPECT_EQ(packet[i], 0u) << "word " << i;
    }
}

TEST_F(Long64, MgSetPkTEX0TexaFormWritesFull64BitWords) {
    alignas(16) u_int packet[24];
    std::memset(packet, 0, sizeof packet);

    ASSERT_EQ(mgSetPkTEX0(packet, kTex0, kTex1, kTexa), 5);

    EXPECT_EQ(ReadWord(packet, 8), kTex1);
    EXPECT_EQ(ReadWord(packet, 10), 0x14ull);
    EXPECT_EQ(ReadWord(packet, 12), kTex0);
    EXPECT_EQ(ReadWord(packet, 14), 6ull);
    EXPECT_EQ(ReadWord(packet, 16), kTexa);
    EXPECT_EQ(ReadWord(packet, 18), 0x3Bull);
    for (int i = 20; i < 24; i++) {
        EXPECT_EQ(packet[i], 0u) << "word " << i;
    }
}

TEST_F(Long64, ConvertBinToTxtRoundTripsFull64BitGroups) {
    // ConvLongToTxt and ConvTxtToLong are file-static, so 64-bit groups go through the
    // public converters, which check every group against its own decode.
    const u64 values[] = {0, 1, kHigh32, 0x100000000ull, 0x0123456789ABCDEFull, 0xFFFFFFFFFFFFFFFFull};

    for (u64 value : values) {
        SCOPED_TRACE(testing::Message() << std::hex << value);

        std::array<u8, 8> bytes{};
        std::memcpy(bytes.data(), &value, sizeof value);

        char text[32] = {};
        ASSERT_EQ(ConvertBinToTxt(bytes.data(), 8, text), 11);

        std::array<u8, 8> decoded{};
        ASSERT_EQ(ConvertTxtToBin(text, decoded.data()), 8);

        u64 out = 0;
        std::memcpy(&out, decoded.data(), sizeof out);
        EXPECT_EQ(out, value);
    }
}

TEST_F(Long64, PasswordEncodeThenDecodeRestoresEveryPayload) {
    std::vector<u8> key = MakePayload(Pattern::kRandom, kKeySize, 0x5EEDu);

    for (Pattern pattern : kPatterns) {
        for (int size : kSizes) {
            SCOPED_TRACE(testing::Message() << "pattern " << static_cast<int>(pattern) << " size " << size);

            const std::vector<u8> payload = MakePayload(pattern, size, 0x1234u + static_cast<u32>(size));
            if (pattern == Pattern::kHighGroups) {
                for (int group = 0; group < size / 8; group++) {
                    EXPECT_GT(GroupValue(payload, group), kHigh32) << "group " << group;
                }
            }

            std::vector<u8>   scrambled = payload;
            std::vector<char> text(static_cast<std::size_t>(TextLength(size) + 1));
            ASSERT_EQ(EncodePassword(scrambled.data(), size, key.data(), kKeySize, text.data(),
                                     static_cast<int>(text.size())),
                      1);

            const std::string password = text.data();
            ASSERT_EQ(password.size(), static_cast<std::size_t>(TextLength(size)));

            std::vector<u8> decoded(static_cast<std::size_t>(size), 0xAA);
            std::string     input = password;
            ASSERT_EQ(DecodePassword(input.data(), decoded.data(), size, key.data(), kKeySize), 1);

            // The last two bytes are the checksum that encoding stored there, not payload.
            EXPECT_TRUE(std::equal(payload.begin(), payload.end() - 2, decoded.begin()));
        }
    }
}

TEST_F(Long64, PasswordSingleCharacterTamperIsRejectedOrChangesBytes) {
    std::vector<u8> key = MakePayload(Pattern::kRandom, kKeySize, 0x5EEDu);

    for (Pattern pattern : kPatterns) {
        for (int size : kSizes) {
            SCOPED_TRACE(testing::Message() << "pattern " << static_cast<int>(pattern) << " size " << size);

            std::vector<u8>   scrambled = MakePayload(pattern, size, 0x1234u + static_cast<u32>(size));
            std::vector<char> text(static_cast<std::size_t>(TextLength(size) + 1));
            ASSERT_EQ(EncodePassword(scrambled.data(), size, key.data(), kKeySize, text.data(),
                                     static_cast<int>(text.size())),
                      1);

            const std::string password = text.data();
            std::vector<u8>   reference(static_cast<std::size_t>(size), 0);
            std::string       original = password;
            ASSERT_EQ(DecodePassword(original.data(), reference.data(), size, key.data(), kKeySize), 1);

            // Decoding is injective over the bytes, and changing one digit changes its 64-bit group,
            // so a tampered text that still passes the checksum must decode to different bytes.
            for (int i = 0; i < TextLength(size); i++) {
                const char *hit = std::strchr(kAlphabet, password[static_cast<std::size_t>(i)]);
                ASSERT_NE(hit, nullptr);
                const int digit = static_cast<int>(hit - kAlphabet);

                std::string tampered = password;
                tampered[static_cast<std::size_t>(i)] = kAlphabet[(digit + 1) % 58];

                std::vector<u8> out(static_cast<std::size_t>(size), 0);
                const int       result = DecodePassword(tampered.data(), out.data(), size, key.data(), kKeySize);

                EXPECT_TRUE(result == 0 || out != reference) << "character " << i;
            }
        }
    }
}
