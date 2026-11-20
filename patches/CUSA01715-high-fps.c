// SPDX-License-Identifier: GPL-2.0-or-later
// God of War III Remastered (CUSA01715) high frame rate fixes for AnyPS5, ported from SpartanPort
// (https://github.com/FelipePrado0/SpartanPort: src/qte_hook.h, src/ledge_hook.h, src/runtime_pad.c).
// Use with CUSA01715-high-fps.toml. Build and run:
//   cc -O2 -shared -fPIC -fno-stack-protector -o gow3-high-fps.so CUSA01715-high-fps.c -lpthread
//   LD_PRELOAD=$PWD/gow3-high-fps.so ./app.elf
//
// QTE safety net: frame counters in the QTE prompt update, timed press, mash and circle struggle
// code; while any of them moves the engine runs at 59.94 FPS, and the player's rate comes back
// 5 s after the last prompt frame (prompts of one sequence come seconds apart). Hold prompts only
// play an animation and are not counted.
// Wall seam fix: the Realm of Hades ledge ray misses through a ~0.015-unit seam that 240 FPS
// steps always land in; a miss within 0.05 units of the last hit on the same object reuses it.
// The hook stubs live in the page gap the loader leaves between the game's code and data: AnyPS5
// reserves the rest of the low address space for the guest.
#define _GNU_SOURCE
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum { KINDS = 4 };
static const int64_t ACTIVE_NS = 150000000, HOLD_NS = 5000000000LL;
static volatile uint32_t counts[KINDS];
static volatile float *engine_fps;
static volatile int32_t *engine_fps_rounded;
static unsigned char *code;
static size_t code_size;
static unsigned char *stubs, *stub_end;

static int find_code(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    uintptr_t next = UINTPTR_MAX;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && !code) {
            code = (unsigned char *)(info->dlpi_addr + p->p_vaddr);
            code_size = p->p_filesz;
        }
    }
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        const uintptr_t start = (info->dlpi_addr + p->p_vaddr) & ~(uintptr_t)4095;
        if (p->p_type == PT_LOAD && code && start > (uintptr_t)code && start < next) next = start;
    }
    const uintptr_t gap = ((uintptr_t)code + code_size + 4095) & ~(uintptr_t)4095;
    if (code && next != UINTPTR_MAX && next >= gap + 4096) stubs = (unsigned char *)gap;
    return 1;
}

static unsigned char *find(const char *pattern, unsigned at) {
    int bytes[64];
    unsigned n = 0;
    for (const char *s = pattern; *s; s += s[2] ? 3 : 2) {
        unsigned value;
        bytes[n++] = s[0] == '?' ? -1 : (sscanf(s, "%2x", &value), (int)value);
    }
    unsigned char *found = NULL;
    for (size_t i = 0; i + n <= code_size; ++i) {
        unsigned j = 0;
        while (j < n && (bytes[j] < 0 || code[i + j] == bytes[j])) ++j;
        if (j == n) {
            if (found) return NULL;
            found = code + i + at;
        }
    }
    return found;
}

static unsigned char *rip(unsigned char *operand, unsigned next) {
    int32_t displacement;
    memcpy(&displacement, operand, 4);
    return operand + next + displacement;
}

static unsigned char *emit(const void *bytes, size_t n) {
    unsigned char *at = stub_end;
    memcpy(stub_end, bytes, n);
    stub_end += n;
    return at;
}

static void emit_u64(uint64_t value) { emit(&value, 8); }

static void emit_jump(const void *to) {
    static const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0};
    emit(jump, sizeof(jump));
    emit_u64((uintptr_t)to);
}

static void emit_count(volatile uint32_t *count) {
    static const unsigned char save[] = {0x48, 0x8d, 0x64, 0x24, 0x80, 0x9c, 0x50, 0x48, 0xb8};
    static const unsigned char restore[] = {0xf0, 0xff, 0x00, 0x58, 0x9d, 0x48, 0x8d, 0xa4, 0x24, 0x80, 0, 0, 0};
    emit(save, sizeof(save));
    emit_u64((uintptr_t)count);
    emit(restore, sizeof(restore));
}

static void write_rel32(unsigned char *site, unsigned char opcode, const void *to, unsigned length) {
    const uintptr_t page = (uintptr_t)site & ~(uintptr_t)4095;
    const size_t span = (uintptr_t)site + length - page;
    mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC);
    const int32_t relative = (int32_t)((intptr_t)to - (intptr_t)(site + 5));
    if (opcode) site[0] = opcode;
    memcpy(site + 1, &relative, 4);
    memset(site + 5, 0x90, length - 5);
    mprotect((void *)page, span, PROT_READ | PROT_EXEC);
}

static int hook_update(void) {
    unsigned char *site = find("55 48 89 e5 41 57 41 56 41 55 41 54 53 50 49 89 ff 49 8b 87 b0 00 00 00 a8 07 0f 85 ?? ?? ?? ?? 48 8d 0d", 0);
    if (!site) return 0;
    unsigned char *stub = stub_end;
    emit_count(&counts[0]);
    emit(site, 6);
    emit_jump(site + 6);
    write_rel32(site, 0xe9, stub, 6);
    return 1;
}

static int hook_timed(void) {
    unsigned char *site = find("66 83 7b 10 03 77 ?? 8b 87 98 00 00 00 c4 e2 78 f2 8f 94 00 00 00 8b 53 10 8d 72 04", 7);
    if (!site) return 0;
    unsigned char *stub = stub_end;
    emit_count(&counts[1]);
    emit(site, 15);
    emit_jump(site + 15);
    write_rel32(site, 0xe9, stub, 15);
    return 1;
}

static int hook_repeat(void) {
    unsigned char *site = find("41 0f b6 77 2c 48 8d 55 cc 48 8b 7d b8 e8 ?? ?? ?? ?? 41 89 c4 45 85 e4", 13);
    if (!site) return 0;
    unsigned char *callee = rip(site + 1, 4);
    unsigned char *stub = stub_end;
    emit_count(&counts[2]);
    emit_jump(callee);
    write_rel32(site, 0, stub, 5);
    return 1;
}

static int hook_circle(void) {
    unsigned char *site = find("48 8b 40 10 48 8b 40 08 f6 80 94 00 00 00 20 74 1d f6 80 98 00 00 00 20 75 14 c4 c1 7a 10 04 24", 8);
    if (!site) return 0;
    static const unsigned char tests[] = {
        0xf6, 0x80, 0x94, 0, 0, 0, 0x20, 0x74, 0x17,
        0xf6, 0x80, 0x98, 0, 0, 0, 0x20, 0x75, 0x0e,
    };
    unsigned char *stub = stub_end;
    emit_count(&counts[3]);
    emit(tests, sizeof(tests));
    emit_jump(site + 18);
    emit_jump(site + 0x26);
    write_rel32(site, 0xe9, stub, 18);
    return 1;
}

static struct {
    volatile uint32_t *result;
    uintptr_t object;
    float position[3];
    uint32_t hit, hits, misses, reused;
    uint32_t cache[24];
} ledge;

__attribute__((noinline, used)) static uint32_t ledge_ray(uintptr_t frame, uintptr_t object, uint32_t hit) {
    if (!(*(const volatile uint32_t *)(object + 0x1c0) & 0x10)) return hit;
    const volatile float *position = (const volatile float *)(frame - 0x230);
    if (hit) {
        for (unsigned i = 0; i < 24; ++i) ledge.cache[i] = ledge.result[i];
        for (unsigned i = 0; i < 3; ++i) ledge.position[i] = position[i];
        ledge.object = object;
        ledge.hit = hit;
        ++ledge.hits;
        return hit;
    }
    ++ledge.misses;
    if (!ledge.hit || ledge.object != object) return 0;
    float distance = 0;
    for (unsigned i = 0; i < 3; ++i) distance += (position[i] - ledge.position[i]) * (position[i] - ledge.position[i]);
    if (!(distance < 0.05f * 0.05f)) return 0;
    for (unsigned i = 0; i < 24; ++i) ledge.result[i] = ledge.cache[i];
    ++ledge.reused;
    return ledge.hit;
}

static int hook_ledge(void) {
    unsigned char *site = find("e8 ?? ?? ?? ?? 31 c9 85 c0 0f 84 ?? ?? ?? ?? 48 8d 05 ?? ?? ?? ?? c5 f8 28 00", 0);
    if (!site) return 0;
    unsigned char *callee = rip(site + 1, 4);
    ledge.result = (volatile uint32_t *)rip(site + 18, 4);
    static const unsigned char call[] = {0xff, 0x15, 2, 0, 0, 0, 0xeb, 8};
    static const unsigned char args[] = {0x48, 0x89, 0xef, 0x48, 0x89, 0xde, 0x89, 0xc2, 0x48, 0xb8};
    static const unsigned char call_rax[] = {0xff, 0xd0};
    unsigned char *stub = stub_end;
    emit(call, sizeof(call));
    emit_u64((uintptr_t)callee);
    emit(args, sizeof(args));
    emit_u64((uintptr_t)ledge_ray);
    emit(call_rax, sizeof(call_rax));
    emit_jump(site + 5);
    write_rel32(site, 0xe9, stub, 5);
    return 1;
}

static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void *qte_watch(void *unused) {
    (void)unused;
    uint32_t seen[KINDS] = {0};
    int64_t last = 0;
    float chosen = 0;
    for (;;) {
        usleep(10000);
        const int64_t now = now_ns();
        for (unsigned k = 0; k < KINDS; ++k) {
            if (counts[k] != seen[k]) { seen[k] = counts[k]; last = now; }
        }
        if (!(*engine_fps > 0.0f)) continue;
        const int active = last && now - last <= ACTIVE_NS;
        if (active && !chosen && *engine_fps > 60.0f) {
            chosen = *engine_fps;
            *engine_fps = 59.94f;
            *engine_fps_rounded = 60;
            fprintf(stderr, "[gow3-high-fps] engine 60 FPS during a QTE (was %.0f)\n", chosen);
        } else if (chosen && now - last > ACTIVE_NS + HOLD_NS) {
            *engine_fps = chosen;
            *engine_fps_rounded = (int32_t)(chosen + 0.5f);
            fprintf(stderr, "[gow3-high-fps] engine back to %.0f FPS after the QTE\n", chosen);
            chosen = 0;
        }
    }
    return NULL;
}

__attribute__((constructor)) static void install(void) {
    dl_iterate_phdr(find_code, NULL);
    unsigned char *setter = code ? find("48 8d 05 ?? ?? ?? ?? c5 fa 11 00 c5 fa 58 0d ?? ?? ?? ?? c5 fa 2c c1 48 8d 0d ?? ?? ?? ?? 89 01", 0) : NULL;
    if (!setter) {
        fprintf(stderr, "[gow3-high-fps] pid %d: no God of War III Remastered 01.00 code in this process, hooks off\n", (int)getpid());
        return;
    }
    engine_fps = (volatile float *)rip(setter + 3, 4);
    engine_fps_rounded = (volatile int32_t *)rip(setter + 26, 4);
    if (!stubs || mmap(stubs, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        fprintf(stderr, "[gow3-high-fps] no page between the game code and data for the hook stubs\n");
        return;
    }
    stub_end = stubs;
    const int qte = hook_update() + hook_timed() + hook_repeat() + hook_circle();
    const int seam = hook_ledge();
    mprotect(stubs, 4096, PROT_READ | PROT_EXEC);
    pthread_t thread;
    if (qte) pthread_create(&thread, NULL, qte_watch, NULL);
    fprintf(stderr, "[gow3-high-fps] QTE safety net: %d of 4 sites; wall seam fix: %s\n", qte, seam ? "ready" : "not found");
}
