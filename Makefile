# TinyRadio build entry points; CI runs the same targets.
#   make                          build every board
#   make test                     host tests
#   make package                  build and package every board into dist/
#   make flash BOARD=v5a          build and flash one board (PORT=... to pick the port)
#   make monitor BOARD=v5a        serial monitor
# PYTHON must be an interpreter that can import platformio (for packaging).

BOARDS ?= ats-mini v5a
OUTPUT ?= dist
PIO ?= pio
PYTHON ?= python3
PORT ?=
ALLOW_DIRTY ?=

pio_envs := $(foreach board,$(BOARDS),-e $(board))
port_flag := $(if $(PORT),--upload-port $(PORT))
monitor_port := $(if $(PORT),--port $(PORT))

.PHONY: all build test package flash monitor clean check-board

all: build

build:
	$(PIO) run $(pio_envs)

test:
	sh tools/test.sh

package: build
	$(PYTHON) tools/package_firmware.py --output $(OUTPUT) \
		$(foreach board,$(BOARDS),--environment $(board)) $(if $(ALLOW_DIRTY),--allow-dirty)

flash: check-board
	$(PIO) run -e $(BOARD) -t upload $(port_flag)

monitor: check-board
	$(PIO) device monitor -e $(BOARD) $(monitor_port)

clean:
	$(PIO) run -t clean $(pio_envs)
	rm -rf $(OUTPUT)

# Flashing the wrong board's image is never what you want, so there is no default.
check-board:
	@test -n "$(BOARD)" || { echo "Set BOARD to one of: $(BOARDS)"; exit 1; }
	@case " $(BOARDS) " in *" $(BOARD) "*) ;; *) echo "Unknown BOARD '$(BOARD)'; use one of: $(BOARDS)"; exit 1;; esac
