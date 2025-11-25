CC = gcc
CFLAGS = -I./src -Wall -Wextra -O2
SRC = src/vision.c src/robots.c src/common.c
OBJ = $(SRC:.c=.o)
BUILD_DIR = build

# detect Windows NT (MSYS/MinGW sets OS variable)
ifeq ($(OS),Windows_NT)
    LIBS = -lws2_32 -lpthread
    EXE_EXT = .exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LIBS = -lpthread
        EXE_EXT =
    else
        LIBS = -lpthread
        EXE_EXT =
    endif
endif

all: $(BUILD_DIR) $(BUILD_DIR)/vision$(EXE_EXT) $(BUILD_DIR)/robots$(EXE_EXT)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/vision$(EXE_EXT): src/vision.c src/common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(BUILD_DIR)/robots$(EXE_EXT): src/robots.c src/common.c
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -rf $(BUILD_DIR) *.o src/*.o

.PHONY: all clean