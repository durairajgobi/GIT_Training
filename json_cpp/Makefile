CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

all: json_demo test_json

json_demo: main.cpp json.cpp json.hpp
	$(CXX) $(CXXFLAGS) main.cpp json.cpp -o $@

test_json: test_json.cpp json.cpp json.hpp
	$(CXX) $(CXXFLAGS) test_json.cpp json.cpp -o $@

run: json_demo
	./json_demo sample.json

test: test_json
	./test_json

clean:
	rm -f json_demo test_json output.json

.PHONY: all run test clean
