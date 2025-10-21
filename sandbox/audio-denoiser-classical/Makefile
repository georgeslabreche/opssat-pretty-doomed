CC = gcc
CFLAGS = -Wall -O2 -std=c99
LIBS = -lsndfile -lfftw3f -lm

SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/spectral_subtraction

# Source files
SOURCES = $(SRCDIR)/spectral_subtraction.c $(SRCDIR)/common/audio_io.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	$(CC) $(OBJECTS) -o $@ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR) $(BUILDDIR)/common
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/common:
	mkdir -p $(BUILDDIR)/common

clean:
	rm -rf $(BUILDDIR)

test: $(TARGET)
	./$(TARGET) samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav output/opssat_sample_denoised_spectral_subtraction.wav

.PHONY: all clean test
