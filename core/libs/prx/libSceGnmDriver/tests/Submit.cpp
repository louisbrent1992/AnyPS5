#include "prx/libSceGnmDriver/libSceGnmDriver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "SceTypes.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<typename TAction>
std::string ExpectFailure(TAction action) {
    try { action(); }
    catch (const std::runtime_error& error) { return error.what(); }
    throw std::runtime_error("expected an exception");
}

constexpr std::uint32_t OpNop = 0x10;
constexpr std::uint32_t Type2Filler = 0x80000000u;
constexpr std::uint32_t Type1Header = 0x40000000u;

constexpr std::uint32_t Type3(std::uint32_t opcode, std::uint32_t bodyWords) {
    return 0xc0000000u | ((bodyWords - 1u) << 16u) | (opcode << 8u);
}

void TestPackets() {
    using GnmDriver::MergeEngines;
    using GnmDriver::PacketAt;
    const std::vector<std::uint32_t> draw{Type2Filler, Type3(OpNop, 1), 0x12345678u, Type2Filler};
    Require(PacketAt(draw, 0, "draw command buffer").size() == 1, "a type-2 dword is not one packet");
    const std::vector<std::uint32_t> constant{Type2Filler, Type3(OpNop, 1), 0xabcdef01u};
    const std::vector<std::uint32_t> expected{Type3(OpNop, 1), 0x12345678u, Type3(OpNop, 1), 0xabcdef01u};
    Require(MergeEngines(draw, constant) == expected, "type-2 filler was not dropped from the draw and constant streams");
    const std::vector<std::uint32_t> typeOne{Type3(OpNop, 1), 0u, Type1Header, 0u};
    const auto message = ExpectFailure([&] { MergeEngines(typeOne, {}); });
    Require(message.find("type 1") != std::string::npos && message.find("0x40000000") != std::string::npos && message.find("dword 2 of 4") != std::string::npos, "a type-1 packet did not throw with its header and position: " + message);
    const std::vector<std::uint32_t> truncated{Type3(OpNop, 3), 0u};
    Require(ExpectFailure([&] { MergeEngines(truncated, {}); }).find("truncated") != std::string::npos, "a truncated type-3 packet did not throw");
}

constexpr std::size_t BlockBytes = 65536;

struct Labels {
    alignas(256) volatile std::uint32_t gate = 0;
    alignas(256) volatile std::uint32_t first = 0;
    alignas(256) volatile std::uint32_t second = 0;
};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "cannot allocate the guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    Labels& Data() { return *reinterpret_cast<Labels*>(block); }

private:
    std::uint8_t* block = nullptr;
};

void TestRewind() {
    std::vector<std::uint32_t> draw{Type3(OpNop, 1), 0x11u, Type3(0x59, 1), 0x80000000u, Type3(OpNop, 1), 0x22u, Type3(0x59, 1), 0u, Type3(OpNop, 1), 0x33u};
    GnmDriver::RewindTail tail;
    const std::vector<std::uint32_t> released{Type3(OpNop, 1), 0x11u, Type3(OpNop, 1), 0x22u};
    Require(GnmDriver::MergeEngines(draw, {}, &tail) == released, "a released REWIND was not dropped or an unreleased one did not end the stream");
    Require(tail.control == &draw[7] && tail.rest.data() == &draw[8] && tail.rest.size() == 2, "an unreleased REWIND did not return the words after it");
}

void TestIndirectBuffers() {
    GuestBlock block;
    auto* words = reinterpret_cast<std::uint32_t*>(&block.Data());
    std::uint32_t* called = words + 1024;
    std::uint32_t* chained = words + 2048;
    called[0] = Type3(OpNop, 1);
    called[1] = 0xaau;
    chained[0] = Type3(OpNop, 1);
    chained[1] = 0xbbu;
    const auto indirect = [](const std::uint32_t* target, std::uint32_t control) {
        const auto address = reinterpret_cast<std::uintptr_t>(target);
        return std::vector<std::uint32_t>{Type3(0x3f, 3), static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), control};
    };
    std::vector<std::uint32_t> draw{Type3(OpNop, 1), 0x11u};
    for (const auto& packet : {indirect(called, 2), indirect(chained, (1u << 20u) | 2u)}) draw.insert(draw.end(), packet.begin(), packet.end());
    draw.insert(draw.end(), {Type3(OpNop, 1), 0xccu});
    const std::vector<std::uint32_t> expected{Type3(OpNop, 1), 0x11u, Type3(OpNop, 1), 0xaau, Type3(OpNop, 1), 0xbbu};
    Require(GnmDriver::MergeEngines(draw, {}) == expected, "a called buffer did not return or a chained buffer did not replace the rest of the draw command buffer");
}

std::uint32_t Low(const volatile void* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

std::uint32_t High(const volatile void* pointer) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)) >> 32u);
}

void WriteData(std::vector<std::uint32_t>& words, volatile std::uint32_t* target, std::uint32_t value) {
    words.insert(words.end(), {0xc0033700u, 0x00100200u, Low(target), High(target), value});
}

void WaitEqual(std::vector<std::uint32_t>& words, volatile std::uint32_t* target, std::uint32_t value) {
    words.insert(words.end(), {0xc0053c00u, 0x13u, Low(target), High(target), value, 0xffffffffu, 0x19u});
}

int Submit(std::vector<std::uint32_t>& words) {
    std::uint32_t* const buffers[1] = {words.data()};
    const std::uint32_t bytes[1] = {static_cast<std::uint32_t>(words.size() * sizeof(std::uint32_t))};
    return sceGnmSubmitCommandBuffers(1, buffers, bytes, nullptr, nullptr);
}

void TestFrameBoundaryWait() {
    GuestBlock block;
    auto& labels = block.Data();
    std::vector<std::uint32_t> first;
    WaitEqual(first, &labels.gate, 1);
    WriteData(first, &labels.first, 1);
    Require(Submit(first) == 0, "the first frame's submission failed");
    Require(sceGnmSubmitDone() == 0, "sceGnmSubmitDone failed");
    std::vector<std::uint32_t> second;
    WriteData(second, &labels.second, 1);
    auto pending = std::async(std::launch::async, [&] { return Submit(second); });
    const bool waited = pending.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout;
    labels.gate = 1;
    Require(pending.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "the second submission did not finish after the gate opened");
    Require(pending.get() == 0, "the second frame's submission failed");
    Require(waited, "the first submission of the next frame did not wait for the work queued before sceGnmSubmitDone");
    Require(labels.first == 1, "the second submission was queued before the first frame's work finished");
    AgcDriverWaitIdle_nid_postfix();
    Require(labels.second == 1, "the second submission did not run");
}

void TestChainedRewinds() {
    GuestBlock block;
    auto& labels = block.Data();
    std::vector<std::uint32_t> first{Type3(0x59, 1), 0u, Type3(OpNop, 1), 0u};
    std::vector<std::uint32_t> second{Type3(0x59, 1), 0u, Type3(0x59, 1), 0u};
    WriteData(second, &labels.first, 1);
    const auto release = [](std::vector<std::uint32_t>& words, std::size_t at) { std::atomic_ref<std::uint32_t>(words[at]).store(0x80000000u, std::memory_order_release); };
    Require(Submit(first) == 0, "the submission ending in an unreleased REWIND failed");
    Require(Submit(second) == 0, "the submission queued behind an unreleased REWIND failed");
    release(first, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    release(second, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    AgcDriverWaitIdle_nid_postfix();
    Require(labels.first == 0, "the words behind a REWIND reached through a queued submission ran before that REWIND was released");
    release(second, 3);
    for (int tries = 0; tries < 500 && labels.first != 1; ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    AgcDriverWaitIdle_nid_postfix();
    Require(labels.first == 1, "the words behind the last REWIND did not run after it was released");
}

void EnableSubmitWaitIdle() {
#ifdef _WIN32
    _putenv_s("APS5_GNM_SUBMIT_WAIT_IDLE", "1");
#else
    setenv("APS5_GNM_SUBMIT_WAIT_IDLE", "1", 1);
#endif
}

}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--packets") {
            TestPackets();
            TestIndirectBuffers();
            TestRewind();
            std::puts("GnmDriver packet tests passed");
            return 0;
        }
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
        EnableSubmitWaitIdle();
        TestFrameBoundaryWait();
        TestChainedRewinds();
        std::puts("GnmDriver submission wait tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
