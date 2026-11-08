# Plain-make build (no CMake needed). Cross-compile with e.g.
#   make CXX=aarch64-linux-gnu-g++
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Os -Wall -Wextra -Wpedantic -Wshadow
EMBEDDED := -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
CPPFLAGS += -Iinclude
LDFLAGS  ?=

LIB_SRC := $(wildcard src/*.cpp)
LIB_OBJ := $(patsubst src/%.cpp,build/obj/%.o,$(LIB_SRC))

all: build/wifi6-comm build/wifi6_tests

build/obj/%.o: src/%.cpp $(wildcard include/wifi6/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(EMBEDDED) -c $< -o $@

build/libwifi6core.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

build/wifi6-comm: app/main.cpp build/libwifi6core.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(EMBEDDED) $< build/libwifi6core.a -Wl,--gc-sections $(LDFLAGS) -o $@

build/wifi6_tests: tests/test_main.cpp build/libwifi6core.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< build/libwifi6core.a $(LDFLAGS) -o $@

test: build/wifi6_tests
	./build/wifi6_tests

sim: build/wifi6-comm
	./build/wifi6-comm --sim

clean:
	rm -rf build

.PHONY: all test sim clean
