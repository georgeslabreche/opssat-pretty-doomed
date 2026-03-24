# PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
#
# Build: make
# Test:  make test

CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

PACKAGE_VERSION ?= v1
PACKAGE_NAME = exp4023-pretty-DOOMed-$(PACKAGE_VERSION)

# Shared headers from common/ (Docker: /app/common, CI/local: ../common)
COMMON_CFLAGS ?= $(if $(wildcard /app/common/include),-I/app/common/include,-I../common/include)

# External library flags
# Local (Debian): pkg-config finds GNU Radio. SEPP (Alpine): pre-built libs at /app/libs-gnuradio.
GNURADIO_CFLAGS = $(shell pkg-config --cflags gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft gnuradio-analog gnuradio-iio 2>/dev/null)
GNURADIO_CFLAGS += $(if $(wildcard /app/libs-gnuradio/include/gnuradio),-I/app/libs-gnuradio/include,)
GNURADIO_LIBS = $(shell pkg-config --libs gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft gnuradio-analog gnuradio-iio 2>/dev/null) -lfmt -liio -lad9361 -lfftw3f
# SEPP: pre-built libs (pkg-config may not have .pc files)
GNURADIO_LIBS += $(if $(wildcard /app/libs-gnuradio/lib/libgnuradio-runtime.so),-L/app/libs-gnuradio/lib -lgnuradio-runtime -lgnuradio-blocks -lgnuradio-filter -lgnuradio-fft -lgnuradio-pmt -lgnuradio-analog -lgnuradio-iio -lvolk,)

# Sherpa-ONNX: two layouts depending on environment.
#
# Local (Debian): installed SDK at /opt/sherpa-onnx with shared libs.
# SEPP (Alpine/musl): built from source at /opt/sherpa-onnx-src with static
#   libs. Static linking is required because the pre-built onnxruntime binary
#   targets glibc — loading it as a shared library segfaults on Alpine/musl.
#   glibc_compat.o stubs resolve the missing symbols at link time.
ifneq ($(wildcard /opt/sherpa-onnx/include/sherpa-onnx/c-api/c-api.h),)
  # Local: installed SDK with shared libs
  SHERPA_CFLAGS = -I/opt/sherpa-onnx/include
  SHERPA_LIBS = -L/opt/sherpa-onnx/lib -lsherpa-onnx-c-api -lonnxruntime
else
  # SEPP: source build with static libs
  SHERPA_SRC = /opt/sherpa-onnx-src
  SHERPA_BUILD = $(SHERPA_SRC)/build
  SHERPA_CFLAGS = -I$(SHERPA_SRC) \
                  -I$(SHERPA_BUILD)/_deps/onnxruntime-src/include
  SHERPA_LIBS = -L$(SHERPA_BUILD)/lib \
                -lsherpa-onnx-c-api \
                -lsherpa-onnx-core \
                -lkaldi-native-fbank-core \
                -lkissfft-float \
                -lkaldi-decoder-core \
                -lsherpa-onnx-kaldifst-core \
                -lsherpa-onnx-fst \
                -lsherpa-onnx-fstfar \
                -lssentencepiece_core \
                -lpiper_phonemize \
                -lespeak-ng \
                -lucd \
                -L$(SHERPA_BUILD)/_deps/onnxruntime-src/lib \
                -lonnxruntime \
                /usr/lib/glibc_compat.o
endif

SNDFILE_LIBS = -lsndfile

SRCDIR = src
BUILDDIR ?= build/local
TESTDIR = tests

# Pure C++17 sources (no external deps - unit testable)
SRCS_PURE = $(SRCDIR)/config.cpp $(SRCDIR)/matcher.cpp $(SRCDIR)/output.cpp $(SRCDIR)/executor.cpp
OBJS_PURE = $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SRCS_PURE))

# Sources with external deps
OBJS_EXT = $(BUILDDIR)/dsp.o $(BUILDDIR)/audio_io.o $(BUILDDIR)/transcriber.o $(BUILDDIR)/capture.o $(BUILDDIR)/pipeline.o $(BUILDDIR)/postcard.o
OBJS_MAIN = $(BUILDDIR)/main.o

TARGET = $(BUILDDIR)/pretty-doomed
TEST_TARGET = $(BUILDDIR)/test_runner

# Doom source (mounted via docker-compose)
DOOM_SRCDIR = doom-src

MODEL_FILES = models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx \
              models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx \
              models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx \
              models/sherpa-onnx/small/tokens.txt

.PHONY: all clean test test-dsp doom clone-sherpa patch-sherpa build-sherpa \
       check-model package-prepare package-input package-model package-demos package-tar

all: check-model $(TARGET) doom

check-model:
	@for f in $(MODEL_FILES); do \
		if [ ! -f "$$f" ]; then \
			echo "ERROR: Missing model file: $$f"; \
			echo ""; \
			echo "Download with:"; \
			echo "  git lfs install"; \
			echo "  git clone https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26 /tmp/sherpa-model"; \
			echo "  mkdir -p models/sherpa-onnx/small"; \
			echo "  cp /tmp/sherpa-model/{encoder-epoch-99-avg-1.int8.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.int8.onnx,tokens.txt} models/sherpa-onnx/small/"; \
			exit 1; \
		fi; \
	done
	@echo "Model files: OK"

#############################################
# Doom binary
#############################################

DOOM_BUILDDIR = /app/doom-build/$(notdir $(BUILDDIR))

doom:
	$(MAKE) -C $(DOOM_SRCDIR) STATIC=YES BUILDDIR=$(DOOM_BUILDDIR)

#############################################
# Main binary
#############################################

$(TARGET): $(OBJS_PURE) $(OBJS_EXT) $(OBJS_MAIN) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(GNURADIO_LIBS) $(SHERPA_LIBS) $(SNDFILE_LIBS) -lpthread -lm \
		-Wl,--allow-shlib-undefined

# Pure C++ objects (no special flags)
$(BUILDDIR)/config.o: $(SRCDIR)/config.cpp $(SRCDIR)/config.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(BUILDDIR)/matcher.o: $(SRCDIR)/matcher.cpp $(SRCDIR)/matcher.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/output.o: $(SRCDIR)/output.cpp $(SRCDIR)/output.h $(SRCDIR)/matcher.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/executor.o: $(SRCDIR)/executor.cpp $(SRCDIR)/executor.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/postcard.o: $(SRCDIR)/postcard.cpp $(SRCDIR)/postcard.h $(SRCDIR)/postcard_image.h $(SRCDIR)/postcard_sc16.h $(SRCDIR)/doom_palette.h $(SRCDIR)/bitmap_font.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) -DPOSTCARD_USE_FFTW -c $< -o $@

# Objects with external deps
$(BUILDDIR)/dsp.o: $(SRCDIR)/dsp.cpp $(SRCDIR)/dsp.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) -c $< -o $@

$(BUILDDIR)/audio_io.o: $(SRCDIR)/audio_io.cpp $(SRCDIR)/audio_io.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/transcriber.o: $(SRCDIR)/transcriber.cpp $(SRCDIR)/transcriber.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) $(SHERPA_CFLAGS) -c $< -o $@

$(BUILDDIR)/capture.o: $(SRCDIR)/capture.cpp $(SRCDIR)/capture.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) $(GNURADIO_CFLAGS) -c $< -o $@

$(BUILDDIR)/pipeline.o: $(SRCDIR)/pipeline.cpp $(SRCDIR)/pipeline.h $(SRCDIR)/audio_io.h $(SRCDIR)/dsp.h $(SRCDIR)/matcher.h $(SRCDIR)/output.h $(SRCDIR)/executor.h $(SRCDIR)/postcard.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) $(GNURADIO_CFLAGS) $(SHERPA_CFLAGS) -c $< -o $@

$(BUILDDIR)/main.o: $(SRCDIR)/main.cpp $(SRCDIR)/config.h $(SRCDIR)/pipeline.h $(SRCDIR)/capture.h $(SRCDIR)/transcriber.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) $(GNURADIO_CFLAGS) $(SHERPA_CFLAGS) -c $< -o $@

#############################################
# Unit tests - pure C++17 modules only
#############################################

TEST_SRCS = $(TESTDIR)/test_main.cpp $(TESTDIR)/test_config.cpp $(TESTDIR)/test_matcher.cpp $(TESTDIR)/test_output.cpp $(TESTDIR)/test_executor.cpp $(TESTDIR)/test_postcard.cpp
TEST_OBJS = $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/%.o,$(TEST_SRCS))

$(BUILDDIR)/test_%.o: $(TESTDIR)/test_%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) -I$(SRCDIR) -I$(TESTDIR) -c $< -o $@

# Test-specific postcard object (no FFTW dependency)
$(BUILDDIR)/postcard_test.o: $(SRCDIR)/postcard.cpp $(SRCDIR)/postcard.h $(SRCDIR)/postcard_image.h $(SRCDIR)/postcard_sc16.h $(SRCDIR)/doom_palette.h $(SRCDIR)/bitmap_font.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS) $(OBJS_PURE) $(BUILDDIR)/postcard_test.o | $(BUILDDIR)
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
# Sherpa-ONNX static library build (ARM32 container)
#
# Built as static libs (BUILD_SHARED_LIBS=OFF) because the pre-built
# onnxruntime binary targets glibc. Loading libonnxruntime.so at runtime
# on Alpine/musl causes a segfault due to deep ABI incompatibilities that
# cannot be resolved with stub libraries. Static linking resolves all
# onnxruntime symbols at link time via glibc_compat.o stubs.
#############################################

SHERPA_VERSION = 1.12.21

# Clone sherpa-onnx source
clone-sherpa:
	@if [ ! -f "$(SHERPA_SRC)/CMakeLists.txt" ]; then \
		echo "Cloning sherpa-onnx v$(SHERPA_VERSION)..."; \
		rm -rf $(SHERPA_SRC)/*; \
		git clone --depth 1 --branch v$(SHERPA_VERSION) \
			https://github.com/k2-fsa/sherpa-onnx.git $(SHERPA_SRC); \
	else \
		echo "Sherpa-ONNX source already exists at $(SHERPA_SRC)"; \
	fi

# Patch source files for musl/Alpine compatibility (fix missing #include <cstdint>)
# GCC 14 on musl requires explicit includes that were implicit before
patch-sherpa: clone-sherpa
	@echo "Patching source files for musl compatibility..."
	@find $(SHERPA_SRC)/sherpa-onnx/csrc -name "*.h" -o -name "*.cc" | while read f; do \
		if grep -q 'int32_t\|int64_t\|uint32_t\|uint64_t' "$$f" 2>/dev/null && \
		   ! grep -q '#include <cstdint>' "$$f" 2>/dev/null; then \
			sed -i '1i #include <cstdint>' "$$f"; \
			echo "  Patched $$(basename $$f)"; \
		fi; \
	done

# Build sherpa-onnx as static libraries
build-sherpa: patch-sherpa
	@if [ ! -f "$(SHERPA_BUILD)/lib/libsherpa-onnx-c-api.a" ]; then \
		echo "Building sherpa-onnx static libs (this may take a while under QEMU)..."; \
		mkdir -p $(SHERPA_BUILD); \
		cd $(SHERPA_BUILD) && cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_SHARED_LIBS=OFF \
			-DSHERPA_ONNX_ENABLE_TESTS=OFF \
			-DSHERPA_ONNX_ENABLE_PYTHON=OFF \
			-DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
			-DSHERPA_ONNX_ENABLE_BINARY=OFF \
			-DCMAKE_EXE_LINKER_FLAGS="/usr/lib/glibc_compat.o" \
			.. && \
		make -j$$(nproc); \
	else \
		echo "sherpa-onnx already built"; \
	fi

#############################################
# SEPP Packaging
#############################################

PACKAGE_DIR = package/$(PACKAGE_NAME)

# Run inside ARM32 container
package-prepare: $(TARGET) doom
	mkdir -p $(PACKAGE_DIR)/libs
	mkdir -p $(PACKAGE_DIR)/models/sherpa-onnx/small
	mkdir -p $(PACKAGE_DIR)/demos
	mkdir -p $(PACKAGE_DIR)/input
	mkdir -p $(PACKAGE_DIR)/toGround
	# Binaries (strip debug symbols + UPX compress for SEPP)
	cp $(TARGET) $(PACKAGE_DIR)/pretty-doomed
	cp $(DOOM_BUILDDIR)/opssat-doom $(PACKAGE_DIR)/
	strip $(PACKAGE_DIR)/pretty-doomed
	strip $(PACKAGE_DIR)/opssat-doom
	upx --best $(PACKAGE_DIR)/pretty-doomed
	upx --best $(PACKAGE_DIR)/opssat-doom
	# Config + scripts
	cp run $(PACKAGE_DIR)/
	cp config.cfg $(PACKAGE_DIR)/
	cp variants.cfg $(PACKAGE_DIR)/
	cp ascii.txt $(PACKAGE_DIR)/
	chmod +x $(PACKAGE_DIR)/run
	# Postcard assets
	mkdir -p $(PACKAGE_DIR)/assets
	cp assets/*.png $(PACKAGE_DIR)/assets/
	# GNU Radio + VOLK + AD9361 libs (pre-built or from /opt/gnuradio)
	cp -P /app/libs-gnuradio/lib/*.so* $(PACKAGE_DIR)/libs/ 2>/dev/null || \
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
	# IIO libs (SDR capture)
	cp -P /usr/lib/libiio.so* $(PACKAGE_DIR)/libs/
	# Sherpa-ONNX + ONNX Runtime: statically linked into pretty-doomed binary
	# (no shared libs needed — see build-sherpa comment)
	@echo "Package prepared in $(PACKAGE_DIR)/"
	@echo "Run outside container: make package-input && make package-model && make package-demos && make package-tar"

# Run OUTSIDE container
package-model:
	cp models/sherpa-onnx/small/*.onnx $(PACKAGE_DIR)/models/sherpa-onnx/small/
	cp models/sherpa-onnx/small/tokens.txt $(PACKAGE_DIR)/models/sherpa-onnx/small/
	@echo "Model files copied to $(PACKAGE_DIR)/models/sherpa-onnx/small/"

package-input:
	cp input/georges_01.wav $(PACKAGE_DIR)/input/
	@echo "Input WAV files copied to $(PACKAGE_DIR)/input/"

package-demos:
	cp demos/*.lmp $(PACKAGE_DIR)/demos/
	cp demos/*.txt $(PACKAGE_DIR)/demos/
	cp demos/doom.wad $(PACKAGE_DIR)/demos/
	@echo "Demo files copied to $(PACKAGE_DIR)/demos/"

# Run OUTSIDE container (BusyBox tar lacks --owner)
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp \
		-czvf package/$(PACKAGE_NAME).tar.gz \
		-C $(PACKAGE_DIR) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz

# Incremental patch package: only changed files.
# Requires package-prepare to have been run first.
# Usage: make package-patch PATCH_FROM=v1 PACKAGE_VERSION=v2 PATCH_FILES="run pretty-doomed config.cfg"
PATCH_FROM ?= v1
PATCH_FILES ?=
PATCH_NAME = exp4023-pretty-DOOMed-$(PATCH_FROM)-to-$(PACKAGE_VERSION)
package-patch:
	@if [ -z "$(PATCH_FILES)" ]; then \
		echo "Error: PATCH_FILES not set."; \
		echo "Usage: make package-patch PATCH_FROM=v1 PACKAGE_VERSION=v2 PATCH_FILES=\"run pretty-doomed config.cfg\""; \
		exit 1; \
	fi
	@for f in $(PATCH_FILES); do \
		if [ ! -e "$(PACKAGE_DIR)/$$f" ]; then \
			echo "Error: $(PACKAGE_DIR)/$$f not found. Run package-prepare first."; \
			exit 1; \
		fi; \
	done
	cd $(PACKAGE_DIR) && COPYFILE_DISABLE=1 tar --owner=exp --group=exp \
		-czvf ../$(PATCH_NAME).tar.gz $(PATCH_FILES)
	@echo "Patch created: package/$(PATCH_NAME).tar.gz"
	@ls -lh package/$(PATCH_NAME).tar.gz

#############################################
# Infrastructure
#############################################

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)/*

clean-all:
	rm -rf build/local/* build/sepp/*
	rm -rf package
