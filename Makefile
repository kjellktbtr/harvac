# Harvac Makefile -- Convenience targets for building and testing

PYTHON  ?= python3
BUILD   ?= $(PYTHON) build.py
NASM    ?= nasm
QEMU    ?= qemu-system-i386
IMAGE   ?= harvac.img

.PHONY: all run qemu display xfer clean

all:
	$(BUILD)

# Build and run in QEMU
run:
	$(BUILD) --run

# Run QEMU without rebuilding (serial stdio, no display)
qemu:
	$(BUILD) --qemu-only

# Run QEMU with VGA display + PS/2 keyboard
display:
	$(BUILD) --display

# Run QEMU with VGA display + COM1 on Unix socket for XFER
xfer:
	$(BUILD) --xfer-run

# Clean generated files
clean:
	$(BUILD) --clean
	rm -f $(IMAGE)

# Syntax/lint check — compiles all source files without linking
check:
	$(BUILD) --check

# Automated QEMU test suite
test:
	$(BUILD) --test