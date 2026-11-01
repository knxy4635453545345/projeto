// language: C++, file: jni/stealth.cpp
// Duas camadas:
//   1. unlink do link_map (solist) — esconde de dlsym/module walkers
//   2. zero do ELF magic na primeira página — derruba scanners que checam o header
#include <link.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <android/log.h>
#include "hellborn.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HB_STEALTH", __VA_ARGS__)

static void unlink_solist() {
    void* self = dlopen("libhellborn.so", RTLD_NOLOAD | RTLD_NOW);
    if (!self) { LOGI("dlopen self failed"); return; }

    struct link_map* lm = nullptr;
    if (dlinfo(self, RTLD_DI_LINKMAP, &lm) != 0 || !lm) return;

    if (lm->l_prev) lm->l_prev->l_next = lm->l_next;
    if (lm->l_next) lm->l_next->l_prev = lm->l_prev;
    if (lm->l_name) lm->l_name[0] = '\0';
    LOGI("solist unlinked");
}

static void zero_elf_header() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libhellborn.so")) {
            sscanf(line, "%lx-", &base);
            break;
        }
    }
    fclose(f);
    if (!base) return;

    long page = sysconf(_SC_PAGESIZE);
    void* p = reinterpret_cast<void*>(base);
    if (mprotect(p, page, PROT_READ | PROT_WRITE) == 0) {
        memset(p, 0, 4);              // apaga o \x7fELF
        mprotect(p, page, PROT_READ);
        LOGI("ELF magic zeroed at %p", p);
    }
}

extern "C" void stealth_hide_module() {
    unlink_solist();
    zero_elf_header();
}