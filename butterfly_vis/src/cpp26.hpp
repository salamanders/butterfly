#pragma once
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string_view>

namespace bf {

// =============================================================================
// Compile-Time Binary Asset Embedding (.incbin -> std::span<const uint8_t>)
// =============================================================================
#define BF_EMBED_BINARY(name, filepath)                                         \
    extern "C" const uint8_t bf_embed_##name##_start[];                         \
    extern "C" const uint8_t bf_embed_##name##_end[];                           \
    __asm__(                                                                    \
        ".section .rodata\n"                                                    \
        ".balign 16\n"                                                          \
        ".global bf_embed_" #name "_start\n"                                    \
        "bf_embed_" #name "_start:\n"                                           \
        ".incbin \"" filepath "\"\n"                                            \
        ".global bf_embed_" #name "_end\n"                                      \
        "bf_embed_" #name "_end:\n"                                             \
        ".byte 0\n"                                                             \
        ".previous\n"                                                           \
    );                                                                          \
    inline std::span<const uint8_t> embedded_##name() noexcept {                \
        return {bf_embed_##name##_start,                                        \
                static_cast<size_t>(bf_embed_##name##_end - bf_embed_##name##_start)}; \
    }                                                                           \
    inline std::string_view embedded_##name##_str() noexcept {                  \
        return {reinterpret_cast<const char*>(bf_embed_##name##_start),         \
                static_cast<size_t>(bf_embed_##name##_end - bf_embed_##name##_start)}; \
    }

} // namespace bf
