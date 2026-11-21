# Host build of the Electron core: make -C components/electron/host run
# (ROM= an absolute path or one relative to this folder)
ROOT      := ../../..
M6502     := ../../../../esp32emu/components/m6502
EMUDBG    := ../../../../esp32emu/components/emudbg
ELK       := ..
BUILD     := build
CFLAGS    := -O2 -g -Wall -Wextra -Wno-unused-parameter -DM6502_HOST_HOOK -DELK_HOST_HOOK -DM6502_SLOW_PAGES -I$(ELK)/include -I$(ELK) \
             -I$(M6502)/include -I$(EMUDBG)/include
ROM       ?= $(ROOT)/roms/os_basic.ic2
ADFS      ?= $(ROOT)/roms/adfs.rom
FRAMES    ?= 300

ELK_SRCS  := $(wildcard $(ELK)/*.c)
ELK_OBJS  := $(patsubst $(ELK)/%.c,$(BUILD)/elk/%.o,$(ELK_SRCS)) $(BUILD)/m6502.o $(BUILD)/m6502_dbg.o \
             $(BUILD)/emudbg.o $(BUILD)/emudbg_posix.o

run: $(BUILD)/elk_host
	./$(BUILD)/elk_host $(ROM) $(FRAMES) $(BUILD)/screen.ppm

$(BUILD)/elk_host: host_main.c $(ELK_OBJS)
	$(CC) $(CFLAGS) $^ -lm -lpthread -lz -o $@

$(BUILD)/elk/%.o: $(ELK)/%.c $(wildcard $(ELK)/*.h $(ELK)/include/*.h) $(M6502)/include/m6502.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(M6502)/%.c $(M6502)/include/m6502.h $(EMUDBG)/include/emudbg.h Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(EMUDBG)/%.c $(EMUDBG)/include/emudbg.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

check: $(BUILD)/elk_host
	python3 check_tape.py ./$(BUILD)/elk_host $(ROM)

check-plus3: $(BUILD)/elk_host
	python3 check_plus3.py ./$(BUILD)/elk_host $(ROM) $(ADFS)

clean:
	rm -rf $(BUILD)

.PHONY: run check check-plus3 clean
