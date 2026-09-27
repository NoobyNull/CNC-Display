# Desktop build of the panel firmware's UI and model layers.
#
#   make            build ./cncsim
#   make run        build, then connect to a fluidnc_sim.py on 127.0.0.1:2323
#   make -j         parallel (LVGL is ~400 translation units, use this)
#
# Everything under src/ui, src/model and src/transport/xmodem_rx is compiled
# from the firmware tree UNMODIFIED. Only the hardware layer is replaced, by
# sim/src/*.cpp. If a build here fails after a firmware change, the firmware
# grew a hardware dependency in a layer that is meant to be portable.

ROOT    := $(abspath $(CURDIR)/..)
DEPS    := $(CURDIR)/deps
BUILD   := $(CURDIR)/build
TARGET  := $(CURDIR)/cncsim

LVGL    := $(DEPS)/lvgl
GRBL    := $(DEPS)/grblparser

FW_TAG  := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo sim)

# Same flags the firmware builds with, so the same code paths compile.
DEFS := -DLV_CONF_INCLUDE_SIMPLE -DJSP_USE_CHARP -DE4_POS_T \
        -DVERBATIM_GCODE_MODES -DFNC_BAUD=1000000 -DFW_TAG=\"$(FW_TAG)-sim\"

INCS := -I$(ROOT)/src -I$(ROOT)/src/hw -I$(CURDIR)/shim \
        -I$(LVGL) -I$(LVGL)/src -I$(GRBL)/src

WARN := -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable

CFLAGS   := -g -O1 $(WARN) $(DEFS) $(INCS) $(shell sdl2-config --cflags)
CXXFLAGS := $(CFLAGS) -std=gnu++17
LDFLAGS  := $(shell sdl2-config --libs) -lm -lpthread

# --- sources ---------------------------------------------------------------
# src/main.cpp, src/hw/* and the UART transport are the hardware half and are
# replaced wholesale by sim/src.
FW_SRC := $(wildcard $(ROOT)/src/ui/*.cpp) \
          $(wildcard $(ROOT)/src/model/*.cpp) \
          $(ROOT)/src/transport/xmodem_rx.cpp

SIM_SRC := $(wildcard $(CURDIR)/src/*.cpp)
LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')
GRBL_SRC := $(wildcard $(GRBL)/src/*.c)

OBJS := $(patsubst $(ROOT)/%,$(BUILD)/fw/%.o,$(FW_SRC)) \
        $(patsubst $(CURDIR)/%,$(BUILD)/sim/%.o,$(SIM_SRC)) \
        $(patsubst $(LVGL)/%,$(BUILD)/lvgl/%.o,$(LVGL_SRC)) \
        $(patsubst $(GRBL)/%,$(BUILD)/grbl/%.o,$(GRBL_SRC))

$(BUILD)/fw/%.cpp.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX fw/$*"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/sim/%.cpp.o: $(CURDIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX $*"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/lvgl/%.c.o: $(LVGL)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/grbl/%.c.o: $(GRBL)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  $*"
	@$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@$(CXX) $(OBJS) $(LDFLAGS) -o $@
	@echo "built $@"

.PHONY: all run clean deps
all: $(TARGET)

run: $(TARGET)
	cd $(ROOT) && $(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET)

# Re-fetch the pinned third-party trees. LVGL is pinned to the same v8.3.11 the
# firmware builds against; a different minor version would render differently
# here than on the panel and make the simulator lie.
deps:
	@mkdir -p $(DEPS)
	@test -d $(LVGL) || git clone --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl.git $(LVGL)
	@test -d $(GRBL) || (git clone https://github.com/MitchBradley/GrblParser.git $(GRBL) && \
	  git -C $(GRBL) checkout -q 9108f54a3e30526b66194898676af34e96a87da9)
