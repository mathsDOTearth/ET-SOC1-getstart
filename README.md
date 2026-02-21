# Getting Started With ET-SOC-1 Development

These instructions are based on my own setup experience as someone new to the ET-Platform ecosystem.  Although you can develop on Windows, MacOS and many other systems, I currently used Ubuntu 25.10, so these instructions are based on that system.

## Install Developer Environment
The first step is to install the ET-SOC-1 developer environment, goto https://github.com/aifoundry-org/et-platform?tab=readme-ov-file#et-platform for instructions on how to install and set up the ET-Platform SDK.

I have also set up the following BASH env vars:
```bash
export ET_TOOLCHAIN=/opt/et
export ET_PLATFORM=/opt/et
export PATH="/opt/et/bin:$PATH"
```

## Setup Build Area
We want to create our own build area withing the et-platform directories, so first step is to set up the environment we need:
```bash
mkdir -p ~/et-platform/ET-SOC1-getstart/{src/testbuild,build,scripts,shared}
cd ~/et-platform/ET-SOC1-getstart
```

The directory name `ET-SOC1-getstart`is used for this area, but I guess it could be anything.  
I want to write a test program to test the build system (`src/testbuild`).  
`build` is used by cmake and `scripts` will be for scripts used to run and benchmark the kernels we write.  

Next we copy shared files which the ET-Platform needs at compile time.  These files are `crt.S` which provides startup code and `sections.ld` which provides link time memory maps (location of things like `.test`, `.data` and the location of the stack).

```bash
cp -v ~/et-platform/test-compute-kernels/src/shared/sections.ld \
      ~/et-platform/ET-SOC1-getstart/shared/sections.ld

cp -v ~/et-platform/test-compute-kernels/src/shared/crt.S \
      ~/et-platform/ET-SOC1-getstart/shared/crt.S
```

## CMAKE files
the ET-Platform SDK uses cmake files so it makes sense that we should used them too.  
Lets create an initial ~/et-platform/ET-SOC1-getstart/CMakeLists.txt file:
```cmake
cmake_minimum_required(VERSION 3.20)
project(ET-SOC1-getstart C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ET_PREFIX "/opt/et" CACHE PATH "ET install prefix")

# ---- Global compile/link flags (close to upstream) ----
add_compile_options(
  --specs=nano.specs
  -mcmodel=medany
  -march=rv64imfc
  -mabi=lp64f
  -mno-strict-align
  -mno-riscv-attribute

  -fstack-usage
  -Wall -Wextra
  -Wdouble-promotion -Wformat -Wnull-dereference -Wswitch-enum -Wshadow -Wstack-protector
  -Wpointer-arith -Wundef -Wbad-function-cast -Wcast-qual -Wcast-align -Wconversion -Wlogical-op
  -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations
  -Wno-main

  -fno-zero-initialized-in-bss
  -ffunction-sections -fdata-sections
)

set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)

add_link_options(
  -nostdlib -nostartfiles
  -Wl,--gc-sections
)

# ---- Paths ----
set(SHARED_DIR "${CMAKE_SOURCE_DIR}/shared")
set(LD_SCRIPT  "${SHARED_DIR}/sections.ld")
set(CM_UMODE_LIB "${ET_PREFIX}/cm-umode/lib/libcm-umode.a")

if(NOT EXISTS "${CM_UMODE_LIB}")
  message(FATAL_ERROR "Missing cm-umode library at: ${CM_UMODE_LIB}")
endif()
if(NOT EXISTS "${LD_SCRIPT}")
  message(FATAL_ERROR "Missing linker script at: ${LD_SCRIPT}")
endif()

# ---- shared_kernel: CRT / low-level glue ----
add_library(shared_kernel STATIC
  "${SHARED_DIR}/crt.S"
)

# crt.S includes etsoc/isa/syscall.h, so it needs cm-umode headers.
target_include_directories(shared_kernel PRIVATE
  "${ET_PREFIX}/cm-umode/include"
)

# ---- Helper to define a kernel ELF ----
function(add_kernel_elf target_name source_file)
  add_executable(${target_name} "${source_file}")

  set_target_properties(${target_name} PROPERTIES
    OUTPUT_NAME "${target_name}"
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/${target_name}"
  )

  # Kernel C sources include etsoc/isa/hart.h etc.
  target_include_directories(${target_name} PRIVATE
    "${ET_PREFIX}/cm-umode/include"
  )

  target_link_options(${target_name} PRIVATE
    -T "${LD_SCRIPT}"
    -Xlinker "-Map=${target_name}.map"
  )

  target_compile_options(${target_name} PRIVATE
    -flto=auto -fno-fat-lto-objects
  )
  target_link_options(${target_name} PRIVATE
    -flto=auto -fno-fat-lto-objects
  )

  # Link order: keep close to upstream link.txt.
  target_link_libraries(${target_name} PRIVATE
    c m gcc
    shared_kernel
    "${CM_UMODE_LIB}"
  )
endfunction()

# ---- Kernels ----
add_kernel_elf(testbuild "${CMAKE_SOURCE_DIR}/src/testbuild/testbuild.c")
```

This cmake file is my best guesswork from looking at other CMakeLists.txt files within the ET-Platform SDK.

# Create Dummy Test C code
To get the initial build setup done, we create a simple test program in C `~/et-platform/ET-SOC1-getstart/src/testbuild/testbuild.c`
```c
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
```
## Build The Test Kernel
Next we use cmake to ceate the build directory contents and compile the code:
```bash
cmake -S . -B build   -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/et/bin/riscv64-unknown-elf-gcc \
  -DCMAKE_ASM_COMPILER=/opt/et/bin/riscv64-unknown-elf-gcc

cmake --build build -j $(nproc)
```

## Run The Test Kernels
Hopefully the build worked and we can run our `testbuild` kernel:
```bash

export ENTRY=$(riscv64-unknown-elf-readelf -h build/src/testbuild/testbuild.elf | \
      awk '/Entry point address:/ {print $4}')

~/et-platform/sw-sysemu/build/sys_emu -l \
  -minions 0x1 -shires 0x1 \
  -reset_pc ${ENTRY} \
  -elf_load build/src/testbuild/testbuild.elf
```

And we should get an output like this:
```
0: INFO EMU: [SYS-EMU] Loading ELF: "build/src/testbuild/testbuild.elf"
0: INFO EMU: [SYSTEM] Segment[0] VA: 0x8005801000	Type: 0x1 (LOAD)
0: INFO EMU: [SYSTEM] Section[0] .text	VMA: 0x8005801000	LMA: 0x8005801000	Size: 0x44	Type: 0x1	Flags: 0x6
0: DEBUG EMU: [H0 S0:N0:C0:T0] Start running
0: DEBUG EMU: [H1 S0:N0:C0:T1] Start running
0: INFO EMU: [SYS-EMU] Starting emulation
0: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801000 (0x00001197) auipc x3,0x1
0: DEBUG EMU: [H0 S0:N0:C0:T0] 	pc : 0x8005801000
0: DEBUG EMU: [H0 S0:N0:C0:T0] 	x3 = 0x8005802000
0: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x8005801000 (0x00001197) auipc x3,0x1
0: DEBUG EMU: [H1 S0:N0:C0:T1] 	pc : 0x8005801000
0: DEBUG EMU: [H1 S0:N0:C0:T1] 	x3 = 0x8005802000
1: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801004 (0x84418193) addi x3,x3,-1980
1: DEBUG EMU: [H0 S0:N0:C0:T0] 	x3 : 0x8005802000
1: DEBUG EMU: [H0 S0:N0:C0:T0] 	x3 = 0x8005801844
1: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x8005801004 (0x84418193) addi x3,x3,-1980
1: DEBUG EMU: [H1 S0:N0:C0:T1] 	x3 : 0x8005802000
1: DEBUG EMU: [H1 S0:N0:C0:T1] 	x3 = 0x8005801844
2: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801008 (0x00e000ef) jal x1,14
2: DEBUG EMU: [H0 S0:N0:C0:T0] 	x1 = 0x800580100c
2: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x8005801008 (0x00e000ef) jal x1,14
2: DEBUG EMU: [H1 S0:N0:C0:T1] 	x1 = 0x800580100c
3: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801016 (0xcd0027f3) csrrs x15,hartid,x0
3: DEBUG EMU: [H0 S0:N0:C0:T0] 	hartid : 0x0
3: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 = 0x0
3: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x8005801016 (0xcd0027f3) csrrs x15,hartid,x0
3: DEBUG EMU: [H1 S0:N0:C0:T1] 	hartid : 0x1
3: DEBUG EMU: [H1 S0:N0:C0:T1] 	x15 = 0x1
4: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x800580101a (0x00008b85) c.andi x15,1
4: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 : 0x0
4: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 = 0x0
4: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x800580101a (0x00008b85) c.andi x15,1
4: DEBUG EMU: [H1 S0:N0:C0:T1] 	x15 : 0x1
4: DEBUG EMU: [H1 S0:N0:C0:T1] 	x15 = 0x1
5: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x800580101c (0x0000ef99) c.bneqz x15,30
5: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 : 0x0
5: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x800580101c (0x0000ef99) c.bneqz x15,30
5: DEBUG EMU: [H1 S0:N0:C0:T1] 	x15 : 0x1
6: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x800580101e (0x000807b7) lui x15,0x80
6: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 = 0x80000
6: DEBUG EMU: [H1 S0:N0:C0:T1] I(M): 0x800580103a (0x10500073) wfi
6: DEBUG EMU: [H1 S0:N0:C0:T1] 	Start waiting for interrupt
6: DEBUG EMU: [H1 S0:N0:C0:T1] Going to sleep
7: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801022 (0x000007c5) c.addi x15,17
7: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 : 0x80000
7: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 = 0x80011
8: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801024 (0x12345737) lui x14,0x12345
8: DEBUG EMU: [H0 S0:N0:C0:T0] 	x14 = 0x12345000
9: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801028 (0x000007d2) c.slli x15,0x14
9: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 : 0x80011
9: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 = 0x8001100000
10: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x800580102a (0x6787071b) addiw x14,x14,1656
10: DEBUG EMU: [H0 S0:N0:C0:T0] 	x14 : 0x12345000
10: DEBUG EMU: [H0 S0:N0:C0:T0] 	x14 = 0x12345678
11: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x800580102e (0x0000c398) c.sd x14,0(x15)
11: DEBUG EMU: [H0 S0:N0:C0:T0] 	x15 : 0x8001100000
11: DEBUG EMU: [H0 S0:N0:C0:T0] 	x14 : 0x12345678
11: DEBUG EMU: [H0 S0:N0:C0:T0] 	MEM32[0x8001100000] = 0x12345678
12: DEBUG EMU: [H0 S0:N0:C0:T0] I(M): 0x8005801030 (0x10500073) wfi
12: DEBUG EMU: [H0 S0:N0:C0:T0] 	Start waiting for interrupt
12: DEBUG EMU: [H0 S0:N0:C0:T0] Going to sleep
13: INFO EMU: [SYS-EMU] Emulation performance: 0.000075 cycles/sec (13 cycles / 0.173574 sec)
13: INFO EMU: [SYS-EMU] Finishing emulation
```
