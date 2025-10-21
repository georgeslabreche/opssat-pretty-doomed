CC = gcc
CFLAGS = -Wall -O2 -std=c99
LIBS = -lsndfile -lfftw3f -lm

SRCDIR = src
BUILDDIR = build
COMMON_OBJS = $(BUILDDIR)/common/audio_io.o

# Targets
TARGET_SPECTRAL = $(BUILDDIR)/spectral_subtraction
TARGET_ADAPTIVE = $(BUILDDIR)/adaptive_filtering

.PHONY: all clean test test-spectral test-adaptive

all: $(TARGET_SPECTRAL) $(TARGET_ADAPTIVE)

# Spectral subtraction
$(TARGET_SPECTRAL): $(BUILDDIR)/spectral_subtraction.o $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LIBS)

# Adaptive filtering
$(TARGET_ADAPTIVE): $(BUILDDIR)/adaptive_filtering.o $(COMMON_OBJS)
	$(CC) $^ -o $@ $(LIBS)

# Object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR) $(BUILDDIR)/common
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/common:
	mkdir -p $(BUILDDIR)/common

clean:
	rm -rf $(BUILDDIR)

# Test targets
test: test-spectral test-adaptive

test-spectral: $(TARGET_SPECTRAL)
	./$(TARGET_SPECTRAL) samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav output/opssat_sample_denoised_spectral_subtraction.wav

test-adaptive: $(TARGET_ADAPTIVE)
	./$(TARGET_ADAPTIVE) samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav output/opssat_sample_denoised_adaptive.wav
