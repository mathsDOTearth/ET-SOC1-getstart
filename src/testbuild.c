#include <etsoc/isa/hart.h>
#include <stdint.h>
#include <stddef.h>

static inline __attribute__((noreturn)) void rich_idle_forever(void)
{
    for (;;) { __asm__ volatile ("wfi"); }
}

static inline void rich_store32(uintptr_t addr, uint32_t v)
{
    __asm__ volatile ("sw %0, 0(%1)" :: "r"(v), "r"(addr) : "memory");
}

__attribute__((noreturn)) void entry_point(const void* a);

__attribute__((noreturn))
void entry_point(const void* a)
{
    (void)a;

    // Only thread 0 performs the observable action; others park.
    if ((int)get_thread_id() != 0) {
        rich_idle_forever();
    }

    rich_store32(0x8001100000ull, 0x12345678u);

    rich_idle_forever();
}
