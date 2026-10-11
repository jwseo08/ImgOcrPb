BUILD_DIR ?= build
CMAKE_ARGS ?=
JOBS ?= 2
PREFIX ?= /usr/local

.PHONY: all configure install clean
all: configure
	cmake --build "$(BUILD_DIR)" --parallel $(JOBS)

configure:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)

install: all
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

clean:
	cmake --build "$(BUILD_DIR)" --target clean

