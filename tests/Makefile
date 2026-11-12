# PC-Tests (Host-Compiler, z. B. MinGW g++ oder clang++ - nicht arm-none-eabi)
#
#   make -C tests            bauen und ausfuehren
#   make -C tests CXX=clang++

CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra

.PHONY: all clean

all: build/droid_link_test
	./build/droid_link_test

build/droid_link_test: droid_link_test.cpp ../firmware/shared/droid_link.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I../firmware/shared droid_link_test.cpp -o $@

clean:
	rm -rf build
