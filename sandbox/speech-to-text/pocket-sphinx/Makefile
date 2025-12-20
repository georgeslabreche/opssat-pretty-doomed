CC = gcc
CFLAGS = -Wall -O2 -std=c99 $(shell pkg-config --cflags pocketsphinx)
LDFLAGS = $(shell pkg-config --libs pocketsphinx)
LIBS = -lsndfile -lm -lpthread

SRCDIR = src
BUILDDIR = build
OUTPUTDIR = output

TRANSCRIBER = $(BUILDDIR)/pocketsphinx_transcriber
WER_CALC = $(BUILDDIR)/wer_calculator

# Sample paths
GEORGES_CLEAN = /samples/georges/georges_opssat_clean.wav
GEORGES_NOISY = /samples/georges/georges_opssat_noisy.wav
GEORGES_VERY_NOISY = /samples/georges/georges_opssat_very_noisy.wav
OPSSAT_SDR = /samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav

# Ground truth (mounted from samples directory)
GROUND_TRUTH = /samples/georges/georges_opssat_transcript.txt

.PHONY: all clean test test-all wer-summary

all: $(TRANSCRIBER) $(WER_CALC)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

$(TRANSCRIBER): $(SRCDIR)/pocketsphinx_transcriber.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

$(WER_CALC): $(SRCDIR)/wer_calculator.c | $(BUILDDIR)
	$(CC) -Wall -O2 -std=c99 $< -o $@

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OUTPUTDIR)

#############################################
# Full Test Suite with Timing
#############################################

test-all: all
	@mkdir -p $(OUTPUTDIR)
	@echo "=== Testing: PocketSphinx (built-in model ~11 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/clean.txt; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/noisy.txt; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/very_noisy.txt; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/very_noisy_time.txt
	@echo ""
	@$(MAKE) wer-summary

#############################################
# WER Calculation
#############################################

wer-summary: $(WER_CALC)
	@echo "=== WER: PocketSphinx ==="
	@echo -n "Clean: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/clean.txt | grep "WER:"
	@echo -n "Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/noisy.txt | grep "WER:"
	@echo -n "Very Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/very_noisy.txt | grep "WER:"
	@echo ""
	@echo "============================================"
	@echo "=== PocketSphinx Complete ==="
	@echo "============================================"
