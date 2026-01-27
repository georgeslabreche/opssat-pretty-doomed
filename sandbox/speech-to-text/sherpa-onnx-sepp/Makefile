# Sherpa-ONNX speech-to-text for OPS-SAT SEPP
#
# Build from source for Alpine ARM32 (musl) compatibility

# Package version (change this for new releases)
PACKAGE_VERSION = v1
PACKAGE_NAME = exp4023-sherpa-onnx-$(PACKAGE_VERSION)

SHERPA_VERSION = 1.12.21
SHERPA_SRC = /opt/sherpa-onnx
SHERPA_BUILD = $(SHERPA_SRC)/build

BUILDDIR = build

.PHONY: all clean clone-sherpa patch-sherpa build-sherpa package-prepare package-model package-tar

all: build-sherpa

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

# Build sherpa-onnx from source (static linking)
# Note: We link against libexecinfo for backtrace() and glibc_compat.o for __libc_single_threaded
#       These are needed because the pre-built onnxruntime library is compiled for glibc
build-sherpa: patch-sherpa
	@if [ ! -f "$(SHERPA_BUILD)/bin/sherpa-onnx-offline" ]; then \
		echo "Building sherpa-onnx (this may take a while with QEMU)..."; \
		mkdir -p $(SHERPA_BUILD); \
		cd $(SHERPA_BUILD) && cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_SHARED_LIBS=OFF \
			-DSHERPA_ONNX_ENABLE_TESTS=OFF \
			-DSHERPA_ONNX_ENABLE_PYTHON=OFF \
			-DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
			-DSHERPA_ONNX_ENABLE_BINARY=ON \
			-DCMAKE_EXE_LINKER_FLAGS="/usr/lib/glibc_compat.o" \
			.. && \
		make -j$$(nproc) sherpa-onnx-offline; \
	else \
		echo "sherpa-onnx-offline already built"; \
	fi
	mkdir -p $(BUILDDIR)
	cp $(SHERPA_BUILD)/bin/sherpa-onnx-offline $(BUILDDIR)/
	strip $(BUILDDIR)/sherpa-onnx-offline

clean:
	rm -rf $(BUILDDIR)
	rm -rf toGround

clean-all: clean
	rm -rf $(SHERPA_SRC)

# Create deployment package for SEPP
# Run inside container: make package-prepare
# Run outside container: make package-model && ./setup-samples.sh && make package-tar
package-prepare: $(BUILDDIR)/sherpa-onnx-offline
	mkdir -p package/$(PACKAGE_NAME)/model
	mkdir -p package/$(PACKAGE_NAME)/input
	cp $(BUILDDIR)/sherpa-onnx-offline package/$(PACKAGE_NAME)/
	cp run package/$(PACKAGE_NAME)/
	chmod +x package/$(PACKAGE_NAME)/run
	chmod +x package/$(PACKAGE_NAME)/sherpa-onnx-offline
	@echo ""
	@echo "Package prepared in package/$(PACKAGE_NAME)/"
	@echo "Run outside container:"
	@echo "  make package-model"
	@echo "  ./setup-samples.sh"
	@echo "  make package-tar"

# Run OUTSIDE the container to copy model files (int8 quantized - smaller)
package-model:
	./setup-model.sh

# Run OUTSIDE the container
package-tar:
	tar --owner=exp --group=exp -czvf package/$(PACKAGE_NAME).tar.gz -C package/$(PACKAGE_NAME) .
	@echo ""
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz
