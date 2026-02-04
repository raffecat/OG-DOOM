################################################################
#
# $Id:$
#
# $Log:$
#
# CC= clang # gcc or g++ or clang

OS_NAME := $(shell uname -s | tr A-Z a-z)
ifeq ($(OS_NAME),linux)

$(info Linux build.)
SDL_I := $(shell sdl2-config --cflags)
SDL_L := $(shell sdl2-config --libs)
ifeq ($(SDL_I),)
$(error "sdl2-config --cflags" didn't return anything - is SDL2 installed?)
endif

else ifeq ($(OS_NAME),darwin)

$(info Mac OS build.)
BREW := $(shell brew --prefix)
ifeq ($(BREW),)
$(error "brew --prefix" didn't return a path - is Homebrew installed?)
endif
SDL := $(shell brew --prefix sdl2)
ifeq ($(SDL),)
$(error "brew --prefix sdl2" didn't return a path - is SDL2 installed?)
endif
SDL_I := -I$(SDL)/include
SDL_L := -L$(SDL)/lib

endif


CFLAGS+=-O2 -Wall \
	-DNORMALUNIX \
	-I../thirdparty/platform \
	-I../thirdparty/LittleMUS \
	-I../thirdparty/Nuked-OPL3 \
	$(SDL_I) \
	# -DUSEASM
LDFLAGS+=$(SDL_L)
LIBS+=-lm -lSDL2 # -lnsl

# subdirectory for objects
O=../build
BIN=..

# not too sophisticated dependency
OBJS=				\
		$(O)/doomdef.o		\
		$(O)/doomstat.o		\
		$(O)/dstrings.o		\
		$(O)/i_system.o		\
		$(O)/i_sound.o		\
		$(O)/i_video.o		\
		$(O)/i_net.o			\
		$(O)/tables.o			\
		$(O)/f_finale.o		\
		$(O)/f_wipe.o 		\
		$(O)/d_main.o			\
		$(O)/d_net.o			\
		$(O)/d_items.o		\
		$(O)/g_game.o			\
		$(O)/m_menu.o			\
		$(O)/m_misc.o			\
		$(O)/m_argv.o  		\
		$(O)/m_bbox.o			\
		$(O)/m_fixed.o		\
		$(O)/m_swap.o			\
		$(O)/m_cheat.o		\
		$(O)/m_random.o		\
		$(O)/am_map.o			\
		$(O)/p_ceilng.o		\
		$(O)/p_doors.o		\
		$(O)/p_enemy.o		\
		$(O)/p_floor.o		\
		$(O)/p_inter.o		\
		$(O)/p_lights.o		\
		$(O)/p_map.o			\
		$(O)/p_maputl.o		\
		$(O)/p_plats.o		\
		$(O)/p_pspr.o			\
		$(O)/p_setup.o		\
		$(O)/p_sight.o		\
		$(O)/p_spec.o			\
		$(O)/p_switch.o		\
		$(O)/p_mobj.o			\
		$(O)/p_telept.o		\
		$(O)/p_tick.o			\
		$(O)/p_saveg.o		\
		$(O)/p_user.o			\
		$(O)/r_bsp.o			\
		$(O)/r_data.o			\
		$(O)/r_draw.o			\
		$(O)/r_main.o			\
		$(O)/r_plane.o		\
		$(O)/r_segs.o			\
		$(O)/r_sky.o			\
		$(O)/r_things.o		\
		$(O)/w_wad.o			\
		$(O)/wi_stuff.o		\
		$(O)/v_video.o		\
		$(O)/st_lib.o			\
		$(O)/st_stuff.o		\
		$(O)/hu_stuff.o		\
		$(O)/hu_lib.o			\
		$(O)/s_sound.o		\
		$(O)/z_zone.o			\
		$(O)/info.o				\
		$(O)/sounds.o		\
		$(O)/musplayer.o		\
		$(O)/musdriver.o		\
		$(O)/opl3.o		\
		$(O)/qrt_system.o		\
		$(O)/qrt_services.o



all:	 $(BIN)/doom

clean:
	rm -f *.o *~ *.flc
	rm -f build/*

$(BIN)/doom:	$(OBJS) $(O)/i_main.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(O)/i_main.o \
	-o $(BIN)/doom $(LIBS)

$(O)/%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/mus%.o:		../thirdparty/LittleMUS/mus%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/opl3.o:		../thirdparty/Nuked-OPL3/opl3.c
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/qrt_%.o:		../thirdparty/platform/qrt_%.c
	$(CC) $(CFLAGS) -c $< -o $@


#############################################################
#
#############################################################
