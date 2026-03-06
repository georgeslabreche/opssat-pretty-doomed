CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17
LDFLAGS = -Wl,-z,stack-size=8388608

# Package version (change this for new releases)
PACKAGE_VERSION = v3
PACKAGE_NAME = exp4023-sdr-loopback-$(PACKAGE_VERSION)

# GNU Radio libraries
GNURADIO_PREFIX = /opt/gnuradio
COMMON_CFLAGS = -I/app/common/include
GNURADIO_CFLAGS = -I$(GNURADIO_PREFIX)/include
GNURADIO_LIBS = -L$(GNURADIO_PREFIX)/lib \
	-lgnuradio-runtime -lgnuradio-blocks -lgnuradio-filter \
	-lgnuradio-fft -lgnuradio-pmt -lgnuradio-analog \
	-lgnuradio-iio \
	-lvolk -liio -lad9361 \
	-lfmt -lsndfile -lboost_system -lboost_filesystem -lpthread

SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/loopback_test

SOURCES = $(SRCDIR)/loopback_test.cpp

.PHONY: all clean test package-prepare package-samples package-tar

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(COMMON_CFLAGS) $(GNURADIO_CFLAGS) $(SOURCES) -o $@ $(GNURADIO_LIBS) $(LDFLAGS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf toGround

# Test with sample file (uses config.cfg defaults)
test: $(TARGET)
	mkdir -p toGround/run-000001
	./$(TARGET) -c config.cfg -i input/georges_opssat_clean.wav -o toGround/run-000001/captured.wav
	@echo "Output: toGround/run-000001/captured.wav"
	@echo "I/Q:    toGround/run-000001/captured.sc16"
	@ls -lh toGround/run-000001/captured.wav toGround/run-000001/captured.sc16 2>/dev/null

# Create deployment package for SEPP
package-prepare: $(TARGET)
	mkdir -p package/$(PACKAGE_NAME)/libs
	mkdir -p package/$(PACKAGE_NAME)/input
	cp $(TARGET) package/$(PACKAGE_NAME)/
	cp run package/$(PACKAGE_NAME)/
	cp config.cfg package/$(PACKAGE_NAME)/
	chmod +x package/$(PACKAGE_NAME)/run
	# Copy GNU Radio + AD9361 libs
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

# Run OUTSIDE the container
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp -czvf package/$(PACKAGE_NAME).tar.gz -C package/$(PACKAGE_NAME) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz
