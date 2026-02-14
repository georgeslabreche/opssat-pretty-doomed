# Sherpa-ONNX speech-to-text for OPS-SAT SEPP
#
# Build sherpa-onnx C API as static library, then compile sherpa-stt against it.
# Static linking is required because the pre-built onnxruntime library targets
# glibc — shared lib loading would fail on Alpine/musl at runtime.
# The C API usage matches pretty-doomed's approach for FlatSat validation.

CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

# Package version (change this for new releases)
PACKAGE_VERSION = v2
PACKAGE_NAME = exp4023-sherpa-onnx-$(PACKAGE_VERSION)

SHERPA_VERSION = 1.12.21
SHERPA_SRC = /opt/sherpa-onnx-src
SHERPA_BUILD = $(SHERPA_SRC)/build

SRCDIR = src
BUILDDIR = build

TARGET = $(BUILDDIR)/sherpa-stt

SHERPA_CFLAGS = -I$(SHERPA_SRC) \
                -I$(SHERPA_BUILD)/_deps/onnxruntime-src/include

PACKAGE_DIR = package/$(PACKAGE_NAME)

.PHONY: all clean clone-sherpa patch-sherpa build-sherpa \
        package-prepare package-model package-tar

all: build-sherpa $(TARGET)

#############################################
# Sherpa-ONNX static library build
#############################################

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

# Build sherpa-onnx as static library
build-sherpa: patch-sherpa
	@if [ ! -f "$(SHERPA_BUILD)/lib/libsherpa-onnx-c-api.a" ]; then \
		echo "Building sherpa-onnx (this may take a while with QEMU)..."; \
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
# sherpa-stt binary
#############################################

# Link statically against sherpa-onnx-c-api and all its dependencies.
# The pre-built onnxruntime.so requires glibc compat stubs at link time.
$(TARGET): $(SRCDIR)/sherpa-stt.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(SHERPA_CFLAGS) $< -o $@ \
		-L$(SHERPA_BUILD)/lib \
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
		/usr/lib/glibc_compat.o \
		-lsndfile -lpthread -lm -lstdc++
	strip $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

#############################################
# SEPP Packaging
#############################################

package-prepare: $(TARGET)
	mkdir -p $(PACKAGE_DIR)/model
	mkdir -p $(PACKAGE_DIR)/input
	mkdir -p $(PACKAGE_DIR)/libs
	mkdir -p $(PACKAGE_DIR)/toGround
	# Binary
	cp $(TARGET) $(PACKAGE_DIR)/
	# Run script
	cp run $(PACKAGE_DIR)/
	chmod +x $(PACKAGE_DIR)/run
	chmod +x $(PACKAGE_DIR)/sherpa-stt
	# Copy audio libs (libsndfile + codecs)
	cp -P /usr/lib/libsndfile.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libogg.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libvorbis.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libvorbisenc.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libFLAC.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libopus.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libmpg123.so* $(PACKAGE_DIR)/libs/
	cp -P /usr/lib/libmp3lame.so* $(PACKAGE_DIR)/libs/
	@echo ""
	@echo "Package prepared in $(PACKAGE_DIR)/"
	@echo "Run outside container:"
	@echo "  make package-model"
	@echo "  ./setup-samples.sh"
	@echo "  make package-tar"

# Run OUTSIDE the container to copy model files
package-model:
	./setup-model.sh

# Run OUTSIDE the container
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp \
		-czvf package/$(PACKAGE_NAME).tar.gz \
		-C $(PACKAGE_DIR) .
	@echo ""
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz

clean:
	rm -rf $(BUILDDIR)/*
	rm -rf toGround

clean-all: clean
	rm -rf package
