# force rebuild, while deps isn't working
.PHONY: default libpce.so clean run runc all ci deps deps-main wrepl

default: libpce.so

all: libpce.so main

deps:
	apt install -y g++
debs-main: deps
	apt install -y libsdl2-dev

ci: deps libpce.so

EMBEDFLAGS=-O3 -fvisibility=hidden -static-libstdc++ -fPIC
# CFLAGS=-fvisibility=hidden -ffreestanding -nostdlib -fPIC -O3 -Wfatal-errors -Werror
SRCS := $(wildcard Geargrafx/src/*.cpp Geargrafx/platforms/libretro/*.cpp) *.o
# some deps are easier to build in c mode, so we'll build a separate obj file in c mode.
CSRCS := -DZ7_ST -c Geargrafx/platforms/shared/dependencies/libchdr/src/*.c  Geargrafx/platforms/shared/dependencies/miniz/*.c Geargrafx/platforms/shared/dependencies/lzma/src/*.c 

PCEFLAGS=-Wfatal-errors -Werror -Wno-narrowing -D__LIBRETRO__ -I Geargrafx/src -I Geargrafx/platforms/libretro -I Geargrafx/platforms/shared/dependencies/miniz/ -I Geargrafx/platforms/shared/dependencies/libchdr/include/ -I Geargrafx/platforms/shared/dependencies/zstd/ -I Geargrafx/platforms/shared/dependencies/lzma/include -Wno-div-by-zero
libpce.so: libpce.cpp corelib.h
	$(CC) $(CFLAGS) $(EMBEDFLAGS) $(PCEFLAGS) $(CSRCS)
	$(CXX) $(CFLAGS) $(EMBEDFLAGS) $(PCEFLAGS) -shared -o libpce.so libpce.cpp $(SRCS)
	cp libpce.so libapu.so
	echo "libpce done"

fast.o: libpce.cpp
	$(CXX) $(CFLAGS) $(PCEFLAGS) -O2 -o fast.o libpce.cpp

main: main.c corelib.h
	$(CXX) -O3 -o main main.c -L. -l:libpce.so -lSDL2 -lc -lm ${WARN}
	echo "main done"

clean:
	rm -f libpce.so *.o

gdb:
	LD_LIBRARY_PATH=$(shell pwd) gdb --args ./main "$(ROM)"
run:
	LD_LIBRARY_PATH=$(shell pwd) ./main "$(ROM)"
runc:
	LD_LIBRARY_PATH=$(shell pwd) ./main "$(ROM)" c

frepl: # fast
	ls libpce.cpp Makefile | entr -c make fast.o
repl:
	ls libpce.cpp Makefile | entr -c make all
wrepl:
	ls libpce.cpp Makefile | entr -c make libpce.js
	
# EMCC=~/external/emsdk/upstream/bin/wasm32-clang++
EMCXX=~/external/emscripten/em++
EMCC=~/external/emscripten/emcc
EXPORTS="['_framebuffer_bytes', '_frame', '_init', '_alloc_rom']"
WASMFLAGS=-Wl,--no-entry -Wl,--export-all -s EXPORTED_FUNCTIONS=$(EXPORTS) -s EXPORTED_RUNTIME_METHODS=['HEAPU8'] -DISWASM

.PHONY: libpce.js
libpce.js:
	$(EMCC) -D__OPTIMIZE__ $(CFLAGS) $(PCEFLAGS) $(CSRCS)
	$(EMCXX) -D__OPTIMIZE__ $(CFLAGS) $(PCEFLAGS) $(WASMFLAGS) -o libpce.js libpce.cpp $(SRCS) 
