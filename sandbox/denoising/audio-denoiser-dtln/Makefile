CC = gcc
CFLAGS = -Wall -O2 -std=c99
LIBS = -lsndfile -lfftw3f -lm
TFLITE_LIBS = -L/opt/lib -ltensorflowlite_c
TFLITE_INCLUDES = -I/opt/include

SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/dtln_denoiser

SOURCES = $(SRCDIR)/dtln_denoiser.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

.PHONY: all clean

all: $(TARGET) test validate

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	$(CC) $(OBJECTS) -o $@ $(LIBS) $(TFLITE_LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(TFLITE_INCLUDES) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

test: $(TARGET)
	./$(TARGET) samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav output/opssat_sample_input_resampled_to_mono.wav output/opssat_sample_denoised.wav

validate: $(TARGET)
	@echo "Testing DTLN with official air conditioning sample..."
	./$(TARGET) samples/dtln/noisy_air_conditioning.wav output/resampled_ac.wav output/dtln_ac_sample_denoised_validation.wav
	@echo "Compare our output (dtln_ac_sample_denoised_validation.wav) with official (samples/dtln/enhanced_air_conditioning.wav)"
	@echo "Files available in output/ directory for audio comparison"

.PHONY: test validate