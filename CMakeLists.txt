# NGP LINK (NelfePlay) : le moteur Beetle NGP avec son port de liaison branche (NGPLINK), et le coeur enveloppe qui
# en porte deux copies. Construit avec MinGW 64 bits (MSYS2) : cmake -G Ninja, puis cmake --build.
cmake_minimum_required(VERSION 3.16)
project(ngp_link C CXX)

set(M ${CMAKE_SOURCE_DIR}/mednafen)
set(N ${M}/ngp)
set(L ${CMAKE_SOURCE_DIR}/libretro-common)

# Le moteur, compile une fois et sans rien exporter : link/prefixer.sh en tire deux copies aux symboles distincts.
add_library(ngp_link_engine OBJECT
  ${N}/sound.cpp ${N}/T6W28_Apu.cpp ${M}/sound/Blip_Buffer.cpp ${M}/mempatcher.cpp ${M}/sound/Stereo_Buffer.cpp
  ${N}/biosHLE.c ${N}/bios.c ${N}/flash.c ${N}/dma.c ${N}/gfx.c ${N}/interrupt.c ${N}/mem.c ${N}/rom.c ${N}/system.c
  ${N}/TLCS-900h/TLCS900h_interpret.c ${N}/TLCS-900h/TLCS900h_interpret_dst.c ${N}/TLCS-900h/TLCS900h_interpret_reg.c
  ${N}/TLCS-900h/TLCS900h_interpret_single.c ${N}/TLCS-900h/TLCS900h_interpret_src.c ${N}/TLCS-900h/TLCS900h_registers.c
  ${M}/hw_cpu/z80-fuse/z80_ops.c ${M}/hw_cpu/z80-fuse/z80.c ${N}/rtc.c ${N}/Z80_interface.c
  ${M}/state.c ${CMAKE_SOURCE_DIR}/libretro.c ${M}/settings.c
  ${L}/streams/file_stream.c ${L}/compat/fopen_utf8.c ${L}/compat/compat_strl.c ${L}/compat/compat_snprintf.c
  ${L}/encodings/encoding_utf.c ${L}/vfs/vfs_implementation.c ${L}/file/file_path.c ${L}/time/rtime.c
  ${L}/string/stdstring.c ${L}/compat/compat_posix_string.c)
target_include_directories(ngp_link_engine PRIVATE ${CMAKE_SOURCE_DIR} ${M} ${M}/include ${M}/intl ${M}/hw_sound
  ${M}/hw_cpu ${M}/hw_misc ${L}/include)
target_compile_definitions(ngp_link_engine PRIVATE NGPLINK WANT_16BPP FRONTEND_SUPPORTS_RGB565 NEED_BLIP
  MEDNAFEN_VERSION_NUMERIC=931 STDC_HEADERS __STDC_LIMIT_MACROS __LIBRETRO__ INLINE=inline RETRO_API= NGPLINK_API=)
# -mcmodel=small : sans les pointeurs .refptr.<variable> que MinGW place en COMDAT, que le renommage ne suit pas.
target_compile_options(ngp_link_engine PRIVATE -O2 -DNDEBUG -w -fno-common -mcmodel=small
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -std=gnu++11> $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)

foreach(copie a b)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/moteur_${copie}.a
    COMMAND bash ${CMAKE_SOURCE_DIR}/link/prefixer.sh ${copie}_ ${CMAKE_BINARY_DIR}/moteur_${copie}.a
      $<TARGET_OBJECTS:ngp_link_engine>
    DEPENDS ngp_link_engine $<TARGET_OBJECTS:ngp_link_engine> ${CMAKE_SOURCE_DIR}/link/prefixer.sh
    COMMAND_EXPAND_LISTS VERBATIM)
endforeach()
add_custom_target(ngp_link_moteurs DEPENDS ${CMAKE_BINARY_DIR}/moteur_a.a ${CMAKE_BINARY_DIR}/moteur_b.a)

add_library(mednafen_ngp_link_libretro SHARED link/ngp_link_libretro.c)
add_dependencies(mednafen_ngp_link_libretro ngp_link_moteurs)
target_include_directories(mednafen_ngp_link_libretro PRIVATE ${L}/include)
target_compile_options(mednafen_ngp_link_libretro PRIVATE -O2 -Wall)
target_link_libraries(mednafen_ngp_link_libretro PRIVATE
  -Wl,--whole-archive ${CMAKE_BINARY_DIR}/moteur_a.a ${CMAKE_BINARY_DIR}/moteur_b.a -Wl,--no-whole-archive)
set_target_properties(mednafen_ngp_link_libretro PROPERTIES PREFIX "" LINKER_LANGUAGE CXX)
target_link_options(mednafen_ngp_link_libretro PRIVATE -static-libgcc -static-libstdc++ -static)
