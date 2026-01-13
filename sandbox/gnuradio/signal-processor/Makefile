CXX = g++
CXXFLAGS = -Wall -O3 -std=c++17

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
	mkdir -p package/exp4023-signal-processor-v1/libs
	mkdir -p package/exp4023-signal-processor-v1/input
	cp $(TARGET) package/exp4023-signal-processor-v1/
	cp run package/exp4023-signal-processor-v1/
	chmod +x package/exp4023-signal-processor-v1/run
	# Copy GNU Radio + VOLK libs (not available on SEPP)
	cp -P $(GNURADIO_PREFIX)/lib/*.so* package/exp4023-signal-processor-v1/libs/
	@echo "Package prepared in package/exp4023-signal-processor-v1/"
	@echo "Run 'make package-samples' then 'make package-tar' outside the container"

# Run OUTSIDE the container to copy sample files
package-samples:
	./setup-samples.sh

# Run OUTSIDE the container (BusyBox tar doesn't support --owner)
package-tar:
	cd package && tar --owner=exp --group=exp -czvf exp4023-signal-processor-v1.tar.gz exp4023-signal-processor-v1/
	@echo "Package created: package/exp4023-signal-processor-v1.tar.gz"
	@ls -lh package/exp4023-signal-processor-v1.tar.gz
