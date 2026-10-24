# Aiie's source lists, for other projects' Makefiles (and this one).
#
# If you're building Aiie into something else (the Electron app, the FPGA
# golden model), include this file and use its variables instead of
# naming Aiie's files yourself, so a file added or moved here doesn't
# break your build:
#
#   include path/to/aiie/aiie.mk
#   CXXFLAGS += $(AIIE_CFLAGS)
#   myprogram: $(MYOBJS) $(AIIE_CORE_OBJS) $(AIIE_NIX_OBJS)
#
# From a shell script, ask the Makefile instead; the paths come out
# relative to wherever you ran it:
#
#   make -s -f path/to/aiie/Makefile print-AIIE_CORE_SRCS
#
#   AIIE_DIR          where Aiie is
#   AIIE_CORE_SRCS    the machine itself: CPU, MMU, cards, BIOS, debugger
#   AIIE_DISK_SRCS    just the Disk II and its disk images (in CORE too)
#   AIIE_NIX_SRCS     the POSIX file manager, clock and prefs
#   AIIE_CFLAGS       the include paths all of those need
#   ..._OBJS          the matching objects, next to their sources
#
# The defines (-DAIIE, -DSTATICALLOC and so on) are left to you, since
# each build picks its own.
#
# The disk image code is the wozzle library, a git submodule of this
# repository. If Aiie is itself a submodule of your project, check it out
# with --recursive.

AIIE_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
# Paths get no prefix at all when we're building in Aiie's own directory
AIIE_P := $(if $(filter .,$(AIIE_DIR)),,$(AIIE_DIR)/)

ifeq ($(wildcard $(AIIE_P)wozzle/wozcore.mk),)
$(error The wozzle submodule is missing from $(AIIE_DIR); run 'git submodule update --init --recursive' in your checkout)
endif
include $(AIIE_P)wozzle/wozcore.mk

AIIE_DISK_SRCS := $(addprefix $(AIIE_P),apple/diskii.cpp apple/woz-serializer.cpp apple/wozhost-aiie.cpp) \
                  $(WOZCORE_SRCS)

AIIE_CORE_SRCS := $(addprefix $(AIIE_P),cpu.cpp apple/appledisplay.cpp apple/applekeyboard.cpp \
                    apple/applemmu.cpp apple/applevm.cpp LRingBuffer.cpp globals.cpp \
                    apple/parallelcard.cpp apple/fx80.cpp lcg.cpp apple/hd32.cpp images.cpp \
                    apple/appleui.cpp vmram.cpp bios.cpp apple/noslotclock.cpp apple/mouse.cpp \
                    physicaldisplay.cpp wsola-speaker.cpp apple/mockingboard.cpp \
                    apple/uthernet2.cpp apple/usernet.cpp debugger.cpp disassembler.cpp) \
                  $(AIIE_DISK_SRCS)

AIIE_NIX_SRCS := $(addprefix $(AIIE_P),nix/nix-filemanager.cpp nix/nix-clock.cpp nix/nix-prefs.cpp)

AIIE_DISK_OBJS := $(addsuffix .o,$(basename $(AIIE_DISK_SRCS)))
AIIE_CORE_OBJS := $(addsuffix .o,$(basename $(AIIE_CORE_SRCS)))
AIIE_NIX_OBJS := $(addsuffix .o,$(basename $(AIIE_NIX_SRCS)))

AIIE_CFLAGS := -I$(AIIE_DIR) -I$(AIIE_P)apple -I$(AIIE_P)nix -I$(AIIE_P)sdl $(WOZCORE_CFLAGS)
