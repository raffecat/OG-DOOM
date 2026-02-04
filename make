#!/usr/bin/env bash

OS_NAME=`uname -s | tr A-Z a-z`
if [ "$OS_NAME" = "linux" ]; then
    SDL_I=`sdl2-config --cflags`
    SDL_L=`sdl2-config --libs`
    if [ "$SDL_I" = "" ]; then
        echo "'sdl2-config --cflags' didn't return anything - is SDL2 installed?)"
        exit 1
    fi
elif [ "$OS_NAME" = "darwin" ]; then
    BREW=`brew --prefix`
    if [ "$BREW" = "" ]; then
        echo "'brew --prefix' didn't return a path - is Homebrew installed?)"
        exit 1
    fi
    SDL=`brew --prefix sdl2`
    if [ "$SDL" = "" ]; then
        echo "'brew --prefix sdl2' didn't return a path - is SDL2 installed?)"
        exit 1
    fi
    SDL_I="-I$SDL/include"
    SDL_L="-L$SDL/lib"
fi

clang -g -Wall -DNORMALUNIX \
  -Ithirdparty/platform \
  -Ithirdparty/LittleMUS \
  -Ithirdparty/Nuked-OPL3 \
  -Ilinuxdoom-1.10 \
  "$SDL_I" \
  "$SDL_L" \
  -lSDL2 \
  linuxdoom-1.10/*.c \
  thirdparty/platform/*.c \
  thirdparty/LittleMUS/*.c \
  thirdparty/Nuked-OPL3/*.c \
  -o doom
