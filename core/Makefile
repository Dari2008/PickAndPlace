# pap-core: pick & place controller for the Raspberry Pi 5 (see ../README.md)
#   make -j4            -> build/pap-core
#   make test           -> build/pap-tests (no hardware needed), runs them
#   make NO_UI=1        -> without the display UI (headless: website / TCP only)
# Needs only g++ (C++17) and make: sudo apt install build-essential

CXX ?= g++
BUILD ?= build
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++17 -Wall -Wno-unused-parameter -Wno-unused-function -pthread -MMD -MP
CPPFLAGS += -Icompat -I.
LDLIBS += -pthread

CORE_SRC := compat/compat.cpp link/record.cpp \
	$(wildcard octo/*.cpp) $(wildcard hal/*.cpp) $(wildcard dev/*.cpp) $(wildcard vision/*.cpp) \
	$(wildcard api/*.cpp) $(wildcard ctrl/*.cpp) $(wildcard ctrl/protocol/*.cpp) \
	$(wildcard ctrl/settings/*.cpp) $(wildcard ctrl/job/*.cpp) \
	$(wildcard third_party/quirc/*.c) third_party/LovyanGFX/src/lgfx/utility/lgfx_qrcode.c

ifeq ($(NO_UI),1)
UI_SRC := ui/ui_stub.cpp
else
LGFX := third_party/LovyanGFX/src
UI_SRC := $(filter-out ui/ui_stub.cpp,$(wildcard ui/*.cpp)) $(wildcard ui/hw/*.cpp) $(wildcard ui/esp/*.cpp) 	$(wildcard ui/machine/*.cpp) $(wildcard ui/link/*.cpp) $(wildcard ui/net/*.cpp) 	$(addprefix $(LGFX)/lgfx/v1/,LGFXBase.cpp lgfx_fonts.cpp lgfx_v1.cpp lgfx_v1_panel.cpp lgfx_v1_platforms.cpp lgfx_v1_touch.cpp) 	$(wildcard $(LGFX)/lgfx/v1/misc/*.cpp) $(filter-out %/lgfx_qrcode.c,$(wildcard $(LGFX)/lgfx/utility/*.c)) $(LGFX)/lgfx/Fonts/lgfx_cjk_stubs.c
CPPFLAGS += -I$(LGFX) -DLGFX_LINUX_FB
endif

MAIN_SRC := main.cpp
TEST_SRC := $(wildcard tests/*.cpp)

OBJ = $(patsubst %.cpp,$(BUILD)/%.o,$(patsubst %.c,$(BUILD)/%.o,$(1)))

all: $(BUILD)/pap-core

$(BUILD)/pap-core: $(call OBJ,$(CORE_SRC) $(UI_SRC) $(MAIN_SRC))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/pap-tests: $(call OBJ,$(CORE_SRC) ui/ui_stub.cpp $(TEST_SRC))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/pap-tests
	cd $(CURDIR) && $(abspath $(BUILD))/pap-tests

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -O2 -g -MMD -MP -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
