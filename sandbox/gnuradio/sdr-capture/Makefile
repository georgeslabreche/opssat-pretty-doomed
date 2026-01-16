CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

# GNU Radio libraries
GNURADIO_PREFIX = /opt/gnuradio
GNURADIO_CFLAGS = -I$(GNURADIO_PREFIX)/include
GNURADIO_LIBS = -L$(GNURADIO_PREFIX)/lib \
	-lgnuradio-runtime -lgnuradio-blocks -lgnuradio-filter \
	-lgnuradio-fft -lgnuradio-pmt -lgnuradio-analog \
	-lgnuradio-iio \
	-lvolk -liio -lad9361 \
	-lfmt -lsndfile -lboost_system -lboost_filesystem -lpthread

SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/capture_loop

SOURCES = $(SRCDIR)/capture_loop.cpp

.PHONY: all clean test package-prepare package-tar

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(GNURADIO_CFLAGS) $(SOURCES) -o $@ $(GNURADIO_LIBS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf toGround

# Test capture (10 seconds)
test: $(TARGET)
	mkdir -p toGround/run-000001
	./$(TARGET) -o toGround/run-000001/capture.wav -d 10
	@echo "Output: toGround/run-000001/capture.wav"

# Create deployment package for SEPP
package-prepare: $(TARGET)
	mkdir -p package/exp4023-sdr-capture-v1/libs
	cp $(TARGET) package/exp4023-sdr-capture-v1/
	cp run package/exp4023-sdr-capture-v1/
	chmod +x package/exp4023-sdr-capture-v1/run
	# Copy GNU Radio + AD9361 libs
	cp -P $(GNURADIO_PREFIX)/lib/*.so* package/exp4023-sdr-capture-v1/libs/
	# Copy Boost libs
	cp -P /usr/lib/libboost_filesystem.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libboost_program_options.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libboost_thread.so* package/exp4023-sdr-capture-v1/libs/
	# Copy core dependencies
	cp -P /usr/lib/libfmt.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libspdlog.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libgmp.so.10* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/liborc-0.4.so* package/exp4023-sdr-capture-v1/libs/
	# Copy FFT libs
	cp -P /usr/lib/libfftw3f.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libfftw3f_threads.so* package/exp4023-sdr-capture-v1/libs/
	# Copy audio libs (libsndfile + codecs)
	cp -P /usr/lib/libsndfile.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libogg.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libvorbis.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libvorbisenc.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libFLAC.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libopus.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libmpg123.so* package/exp4023-sdr-capture-v1/libs/
	cp -P /usr/lib/libmp3lame.so* package/exp4023-sdr-capture-v1/libs/
	@echo "Package prepared in package/exp4023-sdr-capture-v1/"
	@echo "Run 'make package-tar' outside the container"

# Run OUTSIDE the container
package-tar:
	tar --owner=exp --group=exp -czvf package/exp4023-sdr-capture-v1.tar.gz -C package/exp4023-sdr-capture-v1 .
	@echo "Package created: package/exp4023-sdr-capture-v1.tar.gz"
	@ls -lh package/exp4023-sdr-capture-v1.tar.gz
