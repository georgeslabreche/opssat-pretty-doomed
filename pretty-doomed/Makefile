# PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
#
# Build: make
# Test:  make test

CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

PACKAGE_VERSION ?= v3
PACKAGE_NAME = exp4023-pretty-DOOMed-$(PACKAGE_VERSION)

# External library flags
GNURADIO_CFLAGS = $(shell pkg-config --cflags gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft 2>/dev/null)
GNURADIO_LIBS = $(shell pkg-config --libs gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft 2>/dev/null) -lfmt

SHERPA_DIR ?= /opt/sherpa-onnx
SHERPA_CFLAGS = -I$(SHERPA_DIR)/include
SHERPA_LIBS = -L$(SHERPA_DIR)/lib -lsherpa-onnx-c-api

SNDFILE_LIBS = -lsndfile

SRCDIR = src
BUILDDIR = build
TESTDIR = tests

# Pure C++17 sources (no external deps - unit testable)
SRCS_PURE = $(SRCDIR)/config.cpp $(SRCDIR)/matcher.cpp $(SRCDIR)/executor.cpp
OBJS_PURE = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS_PURE))

# Sources with external deps
OBJS_EXT = $(BUILDDIR)/dsp.o $(BUILDDIR)/audio_io.o $(BUILDDIR)/transcriber.o
OBJS_MAIN = $(BUILDDIR)/main.o

TARGET = $(BUILDDIR)/pretty_doomed
TEST_TARGET = $(BUILDDIR)/test_runner

# Doom source (mounted via docker-compose)
DOOM_SRCDIR = doom-src

.PHONY: all clean test test-dsp doom build-sherpa \
       package-prepare package-model package-demos package-tar

all: $(TARGET)

#############################################
# Doom binary
#############################################

doom:
	$(MAKE) -C $(DOOM_SRCDIR) STATIC=YES

#############################################
# Main binary
#############################################

$(TARGET): $(OBJS_PURE) $(OBJS_EXT) $(OBJS_MAIN) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GNURADIO_LIBS) $(SHERPA_LIBS) $(SNDFILE_LIBS) -lpthread -lm

# Pure C++ objects (no special flags)
$(BUILDDIR)/config.o: $(SRCDIR)/config.cpp $(SRCDIR)/config.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/matcher.o: $(SRCDIR)/matcher.cpp $(SRCDIR)/matcher.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/executor.o: $(SRCDIR)/executor.cpp $(SRCDIR)/executor.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Objects with external deps
$(BUILDDIR)/dsp.o: $(SRCDIR)/dsp.cpp $(SRCDIR)/dsp.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) -c $< -o $@

$(BUILDDIR)/audio_io.o: $(SRCDIR)/audio_io.cpp $(SRCDIR)/audio_io.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/transcriber.o: $(SRCDIR)/transcriber.cpp $(SRCDIR)/transcriber.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(SHERPA_CFLAGS) -c $< -o $@

$(BUILDDIR)/main.o: $(SRCDIR)/main.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) $(SHERPA_CFLAGS) -c $< -o $@

#############################################
# Unit tests - pure C++17 modules only
#############################################

TEST_SRCS = $(TESTDIR)/test_main.cpp $(TESTDIR)/test_config.cpp $(TESTDIR)/test_matcher.cpp
TEST_OBJS = $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/%.o,$(TEST_SRCS))

$(BUILDDIR)/test_%.o: $(TESTDIR)/test_%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -I$(TESTDIR) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS) $(OBJS_PURE) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

#############################################
# Integration tests - requires GNU Radio
#############################################

TEST_DSP_TARGET = $(BUILDDIR)/test_dsp_runner

$(TEST_DSP_TARGET): $(BUILDDIR)/test_main.o $(BUILDDIR)/test_dsp.o $(BUILDDIR)/dsp.o | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GNURADIO_LIBS)

$(BUILDDIR)/test_dsp.o: $(TESTDIR)/test_dsp.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) -I$(SRCDIR) -I$(TESTDIR) -c $< -o $@

test-dsp: $(TEST_DSP_TARGET)
	./$(TEST_DSP_TARGET)

#############################################
# Sherpa-ONNX build from source (ARM32 container)
#############################################

SHERPA_VERSION = 1.12.21
SHERPA_SRC = /opt/sherpa-onnx-src

build-sherpa:
	@if [ ! -f "$(SHERPA_DIR)/lib/libsherpa-onnx-c-api.a" ]; then \
		echo "Cloning sherpa-onnx v$(SHERPA_VERSION)..."; \
		git clone --depth 1 --branch v$(SHERPA_VERSION) \
			https://github.com/k2-fsa/sherpa-onnx.git $(SHERPA_SRC); \
		echo "Patching for musl compatibility..."; \
		find $(SHERPA_SRC)/sherpa-onnx/csrc -name "*.h" -o -name "*.cc" | while read f; do \
			if grep -q 'int32_t\|int64_t' "$$f" && ! grep -q '#include <cstdint>' "$$f"; then \
				sed -i '1i #include <cstdint>' "$$f"; \
			fi; \
		done; \
		echo "Building sherpa-onnx (this may take a while under QEMU)..."; \
		mkdir -p $(SHERPA_SRC)/build && cd $(SHERPA_SRC)/build && cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_SHARED_LIBS=OFF \
			-DSHERPA_ONNX_ENABLE_TESTS=OFF \
			-DSHERPA_ONNX_ENABLE_PYTHON=OFF \
			-DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
			-DSHERPA_ONNX_ENABLE_BINARY=OFF \
			-DCMAKE_INSTALL_PREFIX=$(SHERPA_DIR) \
			-DCMAKE_EXE_LINKER_FLAGS="/usr/lib/glibc_compat.o" \
			.. && \
		make -j$$(nproc) && \
		make install; \
	else \
		echo "sherpa-onnx already built at $(SHERPA_DIR)"; \
	fi

#############################################
# SEPP Packaging
#############################################

PACKAGE_DIR = package/$(PACKAGE_NAME)

# Run inside ARM32 container
package-prepare: $(TARGET) doom
	mkdir -p $(PACKAGE_DIR)/libs
	mkdir -p $(PACKAGE_DIR)/model
	mkdir -p $(PACKAGE_DIR)/demos
	mkdir -p $(PACKAGE_DIR)/input
	mkdir -p $(PACKAGE_DIR)/toGround
	# Binaries
	cp $(TARGET) $(PACKAGE_DIR)/pretty_doomed
	cp doom-src/bin/opssat-doom $(PACKAGE_DIR)/
	# Config + scripts
	cp run $(PACKAGE_DIR)/
	cp config.cfg $(PACKAGE_DIR)/
	cp variants.cfg $(PACKAGE_DIR)/
	chmod +x $(PACKAGE_DIR)/run
	# GNU Radio + VOLK libs
	cp -P /opt/gnuradio/lib/*.so* $(PACKAGE_DIR)/libs/
	# Boost libs
	cp -P /usr/lib/libboost_filesystem.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libboost_program_options.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libboost_thread.so* $(PACKAGE_DIR)/libs/
	# Core dependencies
	cp -P /usr/lib/libfmt.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libspdlog.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libgmp.so.10* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/liborc-0.4.so* $(PACKAGE_DIR)/libs/
	# FFT libs
	cp -P /usr/lib/libfftw3f.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libfftw3f_threads.so* $(PACKAGE_DIR)/libs/
	# Audio libs (libsndfile + codecs)
	cp -P /usr/lib/libsndfile.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libogg.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libvorbis.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libvorbisenc.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libFLAC.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libopus.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libmpg123.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libmp3lame.so* $(PACKAGE_DIR)/libs/
	@echo "Package prepared in $(PACKAGE_DIR)/"
	@echo "Run outside container: make package-model && make package-demos && make package-tar"

# Run OUTSIDE container
package-model:
	cp model/*.onnx $(PACKAGE_DIR)/model/
	cp model/tokens.txt $(PACKAGE_DIR)/model/
	@echo "Model files copied to $(PACKAGE_DIR)/model/"

package-demos:
	cp demos/*.lmp $(PACKAGE_DIR)/demos/
	cp demos/doom.wad $(PACKAGE_DIR)/demos/
	@echo "Demo files copied to $(PACKAGE_DIR)/demos/"

# Run OUTSIDE container (BusyBox tar lacks --owner)
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp \
		-czvf package/$(PACKAGE_NAME).tar.gz \
		-C $(PACKAGE_DIR) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz

#############################################
# Infrastructure
#############################################

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

clean-all: clean
	rm -rf package
