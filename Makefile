# if you don't want png support, remove "-DWITHPNG", "-lpng" and "draw_png.cpp" below
CC=g++

CFLAGS=-O3 -std=c++17 -c -Wall -fomit-frame-pointer -pedantic -DWITHPNG -D_FILE_OFFSET_BITS=64 -I/usr/local/include
LDFLAGS=-lz -lpng -L/usr/local/lib

DCFLAGS=-g -O0 -std=c++17 -c -Wall -D_DEBUG -DWITHPNG -D_FILE_OFFSET_BITS=64 -I/usr/local/include
DLDFLAGS=-lz -lpng -L/usr/local/lib

SOURCES=main.cpp helper.cpp colors.cpp worldloader.cpp filesystem.cpp globals.cpp draw_png.cpp settings.cpp

OBJECTS=$(SOURCES:.cpp=.default.o)
DOBJECTS=$(SOURCES:.cpp=.debug.o)

EXECUTABLE=mcmap

# default, zlib and libpng shared
all: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(EXECUTABLE)

# debug, zlib shared
debug: $(DOBJECTS)
	$(CC) $(DOBJECTS) $(DLDFLAGS) -o $(EXECUTABLE)
	#$(CC) $(DOBJECTS) $(DLDFLAGS) -static -o $(EXECUTABLE)

clean:
	rm -f *.o *gch

realClean: clean
	rm -f mcmap output.png defaultcolors.txt

%.default.o: %.cpp
	$(CC) $(CFLAGS) $< -o $@

%.debug.o: %.cpp
	$(CC) $(DCFLAGS) $< -o $@
