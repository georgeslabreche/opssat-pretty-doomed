CC = gcc
CFLAGS = -Wall -O3 -std=c99
LIBS = -lsndfile -lm

SRCDIR = src
BUILDDIR = build
COMMON = $(SRCDIR)/common

# Source files
COMMON_SRC = $(COMMON)/audio_io.c
ALE_SRC = $(SRCDIR)/ale_denoiser.c

# Build targets
TARGET_ALE = $(BUILDDIR)/ale_denoiser

.PHONY: all clean test

all: $(TARGET_ALE)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET_ALE): $(ALE_SRC) $(COMMON_SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(ALE_SRC) $(COMMON_SRC) $(LIBS)

test: $(TARGET_ALE)
	mkdir -p output
	./$(TARGET_ALE) samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav output/opssat_sample_denoised_ale.wav

clean:
	rm -rf $(BUILDDIR) output/*
