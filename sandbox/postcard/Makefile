CXX = g++
CXXFLAGS = -Wall -O2 -std=c++17

SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/postcard

SOURCES = $(SRCDIR)/postcard.cpp

.PHONY: all clean test-jpg test-gif

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $< -o $@ -lm

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Common args for JPG test
JPG_ARGS = --frame samples/jpg-run/impfight/frame-000624.jpg \
	--sc16 samples/capture.sc16 \
	--transcription samples/jpg-run/transcription.txt \
	--scores samples/jpg-run/scores.txt \
	--stats samples/jpg-run/impfight/stats.txt \
	--demo impfight \
	--logo-esa assets/logo-esa.png \
	--logo-doom assets/logo-doom.png \
	--logo-pretty assets/logo-opssat-pretty.png

# Test with JPG frame sample at 1x, 2x, and 3x
test-jpg: $(TARGET)
	./$(TARGET) $(JPG_ARGS) --output build/postcard.png
	./$(TARGET) $(JPG_ARGS) --scale 2 --output build/postcard-2x.png
	./$(TARGET) $(JPG_ARGS) --scale 3 --output build/postcard-3x.png

# Common args for GIF test
GIF_ARGS = --frame samples/gif-run/e1m7-607/frames-007992-008025.gif \
	--sc16 samples/capture.sc16 \
	--transcription samples/gif-run/transcription.txt \
	--scores samples/gif-run/scores.txt \
	--stats samples/gif-run/e1m7-607/stats.txt \
	--demo e1m7-607 \
	--logo-esa assets/logo-esa.png \
	--logo-doom assets/logo-doom.png \
	--logo-pretty assets/logo-opssat-pretty.png

# Test with GIF frame sample at 1x, 2x, and 3x
test-gif: $(TARGET)
	./$(TARGET) $(GIF_ARGS) --output build/postcard.png
	./$(TARGET) $(GIF_ARGS) --scale 2 --output build/postcard-2x.png
	./$(TARGET) $(GIF_ARGS) --scale 3 --output build/postcard-3x.png

clean:
	rm -rf $(BUILDDIR)/*
