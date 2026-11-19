#ifndef CORE_LIBS_PRX_LIBSCEGNMDRIVER_INCLUDE_COMMANDSTREAM_HPP
#define CORE_LIBS_PRX_LIBSCEGNMDRIVER_INCLUDE_COMMANDSTREAM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace GnmDriver {

std::span<const std::uint32_t> PacketAt(std::span<const std::uint32_t> words, std::size_t at, const char* stream);
// A REWIND the title has not released yet: the draw engine stalls on its control word and then
// reads the words after it, which the title may still be writing.
struct RewindTail {
    const std::uint32_t* control = nullptr;
    std::span<const std::uint32_t> rest;
};

// Lowers the draw and constant streams. A REWIND that is not valid yet ends the result and is
// returned in tail; without tail it is not implemented.
std::vector<std::uint32_t> MergeEngines(std::span<const std::uint32_t> dcb, std::span<const std::uint32_t> ccb, RewindTail* tail = nullptr);

}

#endif
