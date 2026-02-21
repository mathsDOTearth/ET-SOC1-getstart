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
