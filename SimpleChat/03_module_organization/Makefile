# Plain-make alternative to CMake:  make && ./simple_chat
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CPPFLAGS += -Iinclude

CORE := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
CORE_OBJ := $(CORE:src/%.cpp=build/%.o)

all: simple_chat

simple_chat: $(CORE_OBJ) build/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

simplechat_tests: $(CORE_OBJ) build/test_main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test: simplechat_tests
	./simplechat_tests

build/%.o: src/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/test_main.o: tests/test_main.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build simple_chat simplechat_tests

.PHONY: all test clean
