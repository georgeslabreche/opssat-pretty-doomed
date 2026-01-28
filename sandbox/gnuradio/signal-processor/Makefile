CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

# Package version (change this for new releases)
PACKAGE_VERSION = v2
PACKAGE_NAME = exp4023-signal-processor-$(PACKAGE_VERSION)

# GNU Radio libraries from /opt/gnuradio (copied from build-libs-armv7)
GNURADIO_PREFIX = /opt/gnuradio
GNURADIO_CFLAGS = -I$(GNURADIO_PREFIX)/include
GNURADIO_LIBS = -L$(GNURADIO_PREFIX)/lib \
	-lgnuradio-runtime -lgnuradio-blocks -lgnuradio-filter \
	-lgnuradio-fft -lgnuradio-pmt -lgnuradio-analog \
	-lvolk -lfmt -lsndfile -lboost_system -lboost_filesystem

SRCDIR = src
BUILDDIR = build
OUTPUTDIR = io/output
TARGET = $(BUILDDIR)/signal_processor

SOURCES = $(SRCDIR)/signal_processor.cpp

.PHONY: all clean test run-clean run-noisy run-very-noisy package

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) $(SOURCES) -o $@ $(GNURADIO_LIBS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OUTPUTDIR)/*.wav

# Run with clean input
run-clean: $(TARGET) | $(OUTPUTDIR)
	./$(TARGET) \
		-i samples/georges/georges_opssat_clean.wav \
		-o $(OUTPUTDIR)/processed_georges_opssat_clean.wav
	@echo "Output: $(OUTPUTDIR)/processed_georges_opssat_clean.wav"

# Run with noisy input
run-noisy: $(TARGET) | $(OUTPUTDIR)
	./$(TARGET) \
		-i samples/georges/georges_opssat_noisy.wav \
		-o $(OUTPUTDIR)/processed_georges_opssat_noisy.wav
	@echo "Output: $(OUTPUTDIR)/processed_georges_opssat_noisy.wav"

# Run with very noisy input
run-very-noisy: $(TARGET) | $(OUTPUTDIR)
	./$(TARGET) \
		-i samples/georges/georges_opssat_very_noisy.wav \
		-o $(OUTPUTDIR)/processed_georges_opssat_very_noisy.wav
	@echo "Output: $(OUTPUTDIR)/processed_georges_opssat_very_noisy.wav"

# Run all
test: run-clean run-noisy run-very-noisy
	@echo "All outputs created in $(OUTPUTDIR)/"

# Create deployment package for SEPP
# Run inside container: make package-prepare
# Run outside container: make package-samples && make package-tar
package-prepare: $(TARGET)
	mkdir -p package/$(PACKAGE_NAME)/libs
	mkdir -p package/$(PACKAGE_NAME)/input
	mkdir -p package/$(PACKAGE_NAME)/toGround
	cp $(TARGET) package/$(PACKAGE_NAME)/
	cp run package/$(PACKAGE_NAME)/
	chmod +x package/$(PACKAGE_NAME)/run
	# Copy GNU Radio + VOLK libs
	cp -P $(GNURADIO_PREFIX)/lib/*.so* package/$(PACKAGE_NAME)/libs/
	# Copy Boost libs
	cp -P /usr/lib/libboost_filesystem.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libboost_program_options.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libboost_thread.so* package/$(PACKAGE_NAME)/libs/
	# Copy core dependencies
	cp -P /usr/lib/libfmt.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libspdlog.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libgmp.so.10* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/liborc-0.4.so* package/$(PACKAGE_NAME)/libs/
	# Copy FFT libs
	cp -P /usr/lib/libfftw3f.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libfftw3f_threads.so* package/$(PACKAGE_NAME)/libs/
	# Copy audio libs (libsndfile + codecs)
	cp -P /usr/lib/libsndfile.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libogg.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libvorbis.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libvorbisenc.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libFLAC.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libopus.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libmpg123.so* package/$(PACKAGE_NAME)/libs/
	cp -P /usr/lib/libmp3lame.so* package/$(PACKAGE_NAME)/libs/
	@echo "Package prepared in package/$(PACKAGE_NAME)/"
	@echo "Run 'make package-samples' then 'make package-tar' outside the container"

# Run OUTSIDE the container to copy sample files
package-samples:
	./setup-samples.sh

# Run OUTSIDE the container (BusyBox tar doesn't support --owner)
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp -czvf package/$(PACKAGE_NAME).tar.gz -C package/$(PACKAGE_NAME) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz
