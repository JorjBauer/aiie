#!/bin/bash
# Build aiie -> WebAssembly (SDL2 -> canvas). Run from the aiie repo root.
set -e
cd "$(dirname "$0")/.."
COMMON="cpu.cpp apple/appledisplay.cpp apple/applekeyboard.cpp apple/applemmu.cpp apple/applevm.cpp apple/diskii.cpp apple/nibutil.cpp LRingBuffer.cpp globals.cpp apple/parallelcard.cpp apple/fx80.cpp lcg.cpp apple/hd32.cpp images.cpp apple/appleui.cpp vmram.cpp bios.cpp apple/noslotclock.cpp apple/woz.cpp apple/crc32.c apple/woz-serializer.cpp apple/mouse.cpp physicaldisplay.cpp wsola-speaker.cpp apple/mockingboard.cpp apple/uthernet2.cpp apple/usernet.cpp"
# wasm-safe SDL frontend (excludes sdl-uthernet2 + usernet-bsd -- BSD sockets)
SDL="sdl/sdl-speaker.cpp sdl/sdl-display.cpp sdl/sdl-keyboard.cpp sdl/sdl-paddles.cpp nix/nix-filemanager.cpp sdl/aiie.cpp sdl/sdl-printer.cpp nix/nix-clock.cpp nix/nix-prefs.cpp nix/debugger.cpp nix/disassembler.cpp sdl/sdl-mouse.cpp"
emcc -O2 -I.. -I. -Iapple -Inix -Isdl \
  -DSUPPRESSREALTIME -DSTATICALLOC -DAIIE \
  -sUSE_SDL=2 -sUSE_ZLIB=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY=0 \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,callMain,HEAPU8 -sMODULARIZE=1 -sEXPORT_NAME=createAiie -sINVOKE_RUN=0 -sFORCE_FILESYSTEM=1 -sSINGLE_FILE=1 -sEXPORTED_FUNCTIONS=_main,_aiie_inject,_aiie_cycles,_aiie_peek,_aiie_poke,_aiie_poke_block,_aiie_save_state,_aiie_load_state,_aiie_get_pc,_aiie_set_pc,_aiie_set_speed,_aiie_get_speed,_aiie_audio_pull,_aiie_audio_avail,_aiie_run_cycles,_aiie_set_audiopaced,_aiie_set_volume,_aiie_get_volume,_aiie_track_bits,_malloc,_free \
  \
  $COMMON $SDL \
  -o wasm/aiie-single.js
