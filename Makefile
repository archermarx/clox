# Our compiler
CC=ccache clang
# Enabled warnings
WARNINGS=-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wfatal-errors -Wimplicit-fallthrough -Werror

# Specifically disabled warnings
DISABLED_WARNINGS=-Wno-declaration-after-statement -Wno-newline-eof -Wno-padded -Wno-missing-noreturn

# Include directories
INCLUDES=-I/home/marksta/include

# Remaining flags
DEBUG_FLAGS=
CFLAGS=-gdwarf-4 $(WARNINGS) $(DISABLED_WARNINGS) $(INCLUDES) $(DEBUG_FLAGS) -B/usr/local/libexec/mold -std=c2x -O2

# Linked libraries
LIBS=

# File setup
BUILD_DIR=build
SRC_DIR=src

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:%.o=%.d)

# Executable name
EXE = clox

.PHONY: clean remake debug

# Default target named after binary
$(EXE): $(BUILD_DIR)/$(EXE)

# Binary build target - depends on all .o files.
$(BUILD_DIR)/$(EXE): $(OBJS) $(BUILD_DIR)/linenoise.o
	@mkdir -p $(@D)
	@ $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# compile the linenoise library
$(BUILD_DIR)/linenoise.o: linenoise/linenoise.c
	@mkdir -p $(@D)
	@ $(CC) $(CFLAGS) -MMD -c  $< -o $@ 
 
#include all .d files
-include $(DEPS) linenoise.d

# Build target for each object file
# The potential dependency on header files is covered by calling `include $(DEPS)`
# The -MMD flags create .d files with the same name as the .o files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo $<
	@ $(CC) $(CFLAGS) -MMD -c  $< -o $@ -Ilinenoise

run: $(EXE)
	@./$(BUILD_DIR)/$(EXE)

test: $(EXE)
	@./$(BUILD_DIR)/$(EXE) -t

clean:
	@ rm -rf $(BUILD_DIR)/$(EXE) $(OBJS) $(DEPS)

debug:
	@echo srcs = $(SRCS)
	@echo objs = $(OBJS)
	@echo deps = $(DEPS)

remake: clean $(EXE)
