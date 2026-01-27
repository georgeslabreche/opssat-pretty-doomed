CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

# Package version (change this for new releases)
PACKAGE_VERSION = v1
PACKAGE_NAME = exp4023-gnuradio-whisper-$(PACKAGE_VERSION)

# GNU Radio flags
GNURADIO_CFLAGS = $(shell pkg-config --cflags gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft 2>/dev/null)
GNURADIO_LIBS = $(shell pkg-config --libs gnuradio-runtime gnuradio-blocks gnuradio-filter gnuradio-fft 2>/dev/null) -lfmt

# Whisper flags
WHISPER_CFLAGS = -I/usr/local/include
WHISPER_LIBS = -L/usr/local/lib -lwhisper -lm -lpthread

SRCDIR = src
BUILDDIR = build
OUTPUTDIR = output
MODELDIR = models

# Single pipeline binary
PIPELINE = $(BUILDDIR)/gnuradio_pipeline

# Model paths
LOCAL_MODEL = $(MODELDIR)/ggml-tiny.bin
DOCKER_MODEL = /opt/whisper.cpp/models/ggml-tiny.bin
WHISPER_MODEL = $(shell if [ -f $(LOCAL_MODEL) ]; then echo $(LOCAL_MODEL); else echo $(DOCKER_MODEL); fi)

# Sample paths - OPS-SAT
NOISY_SAMPLE = samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav
DENOISED_SAMPLE = samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF_DENOISED.wav

# Sample paths - Georges
GEORGES_CLEAN = samples/georges/georges_opssat_clean.wav
GEORGES_NOISY = samples/georges/georges_opssat_noisy.wav
GEORGES_VERY_NOISY = samples/georges/georges_opssat_very_noisy.wav

.PHONY: all clean test test-compare test-georges-full test-opssat copy-libs package download-model

# Build pipeline
all: $(PIPELINE)

$(PIPELINE): $(SRCDIR)/gnuradio_pipeline.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) $(WHISPER_CFLAGS) $< -o $@ $(GNURADIO_LIBS) $(WHISPER_LIBS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

$(MODELDIR):
	mkdir -p $(MODELDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OUTPUTDIR)/*.txt
	rm -rf $(OUTPUTDIR)/*.wav

#############################################
# Pipeline Tests
#############################################

# Basic test
test: $(PIPELINE) | $(OUTPUTDIR)
	@echo "=== Running Pipeline Test ==="
	./$(PIPELINE) \
		-i $(GEORGES_NOISY) \
		-o $(OUTPUTDIR)/transcription.txt \
		-m $(WHISPER_MODEL) \
		-n 0.5

# Compare: Clean vs Noisy vs Very Noisy
test-compare: $(PIPELINE) | $(OUTPUTDIR)
	@echo "============================================"
	@echo "=== COMPARISON: Clean vs Noisy ==="
	@echo "============================================"
	@echo ""
	@echo "--- CLEAN ---"
	./$(PIPELINE) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/t_clean.txt -m $(WHISPER_MODEL) -n 0.0
	@echo ""
	@echo "--- NOISY ---"
	./$(PIPELINE) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/t_noisy.txt -m $(WHISPER_MODEL) -n 0.5
	@echo ""
	@echo "--- VERY NOISY ---"
	./$(PIPELINE) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/t_very_noisy.txt -m $(WHISPER_MODEL) -n 0.7

# Full test with all georges samples
test-georges-full: $(PIPELINE) | $(OUTPUTDIR)
	@echo "============================================"
	@echo "=== FULL COMPARISON: All Georges Samples ==="
	@echo "============================================"
	@echo ""
	@echo "1. Clean"
	./$(PIPELINE) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/t_clean.txt -m $(WHISPER_MODEL) -n 0.0
	@echo ""
	@echo "2. Noisy"
	./$(PIPELINE) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/t_noisy.txt -m $(WHISPER_MODEL) -n 0.5
	@echo ""
	@echo "3. Very Noisy"
	./$(PIPELINE) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/t_very_noisy.txt -m $(WHISPER_MODEL) -n 0.7
	@echo ""
	@echo "============================================"
	@echo "=== SUMMARY ==="
	@echo "============================================"
	@echo ""
	@echo "CLEAN:"
	@cat $(OUTPUTDIR)/t_clean.txt
	@echo ""
	@echo "NOISY:"
	@cat $(OUTPUTDIR)/t_noisy.txt
	@echo ""
	@echo "VERY NOISY:"
	@cat $(OUTPUTDIR)/t_very_noisy.txt

# Test with save processed audio
test-with-audio: $(PIPELINE) | $(OUTPUTDIR)
	./$(PIPELINE) \
		-i $(GEORGES_NOISY) \
		-o $(OUTPUTDIR)/transcription.txt \
		-p $(OUTPUTDIR)/processed.wav \
		-m $(WHISPER_MODEL) \
		-n 0.5 \
		-v

#############################################
# OPS-SAT Sample Tests
#############################################

test-opssat: $(PIPELINE) | $(OUTPUTDIR)
	@echo "=== Testing with OPS-SAT sample ==="
	./$(PIPELINE) \
		-i $(NOISY_SAMPLE) \
		-o $(OUTPUTDIR)/opssat_transcription.txt \
		-m $(WHISPER_MODEL) \
		-n 0.6

#############################################
# Deployment
#############################################

# Download model
download-model: | $(MODELDIR)
	@if [ ! -f $(LOCAL_MODEL) ]; then \
		echo "Downloading Whisper tiny model..."; \
		wget -O $(LOCAL_MODEL) "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin"; \
	else \
		echo "Model already exists at $(LOCAL_MODEL)"; \
	fi

# Copy required shared libraries for deployment
copy-libs: $(PIPELINE)
	mkdir -p libs
	@echo "Copying shared libraries..."
	ldd $(PIPELINE) | grep -E 'gnuradio|volk|boost|fftw|whisper|ggml' | awk '{print $$3}' | xargs -I{} cp -v {} libs/ 2>/dev/null || true
	@echo "Libraries copied to libs/"

# Create deployment package for SEPP
package: $(PIPELINE) copy-libs download-model
	mkdir -p package/$(PACKAGE_NAME)/libs
	mkdir -p package/$(PACKAGE_NAME)/models
	cp $(PIPELINE) package/$(PACKAGE_NAME)/
	cp run package/$(PACKAGE_NAME)/
	cp -r libs/* package/$(PACKAGE_NAME)/libs/ 2>/dev/null || true
	cp $(LOCAL_MODEL) package/$(PACKAGE_NAME)/models/
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp -czvf package/$(PACKAGE_NAME).tar.gz -C package/$(PACKAGE_NAME) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
