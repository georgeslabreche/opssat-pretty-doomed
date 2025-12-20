CC = gcc
CFLAGS = -Wall -O2 -std=c99
LIBS = -lvosk -lsndfile -lm -lpthread

SRCDIR = src
BUILDDIR = build
OUTPUTDIR = output
MODELDIR = models

TRANSCRIBER = $(BUILDDIR)/vosk_transcriber
WER_CALC = $(BUILDDIR)/wer_calculator

# Sample paths
GEORGES_CLEAN = /samples/georges/georges_opssat_clean.wav
GEORGES_NOISY = /samples/georges/georges_opssat_noisy.wav
GEORGES_VERY_NOISY = /samples/georges/georges_opssat_very_noisy.wav
OPSSAT_SDR = /samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav

# Model paths
MODEL_SMALL = $(MODELDIR)/vosk-model-small-en-us-0.15
MODEL_LGRAPH = $(MODELDIR)/vosk-model-en-us-0.22-lgraph

# Ground truth (mounted from samples directory)
GROUND_TRUTH = /samples/georges/georges_opssat_transcript.txt

.PHONY: all clean download-models download-model-small download-model-lgraph \
        test-all-models wer-summary-all

all: $(TRANSCRIBER) $(WER_CALC)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

$(TRANSCRIBER): $(SRCDIR)/vosk_transcriber.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

$(WER_CALC): $(SRCDIR)/wer_calculator.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OUTPUTDIR)

#############################################
# Model Downloads
#############################################

download-models: download-model-small download-model-lgraph

download-model-small:
	@if [ ! -d "$(MODEL_SMALL)" ]; then \
		echo "Downloading Vosk SMALL model (~40 MB)..."; \
		mkdir -p $(MODELDIR); \
		wget -q --show-progress -O $(MODELDIR)/model-small.zip \
			"https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"; \
		cd $(MODELDIR) && unzip -q model-small.zip && rm model-small.zip; \
	else \
		echo "Model already exists at $(MODEL_SMALL)"; \
	fi

download-model-lgraph:
	@if [ ! -d "$(MODEL_LGRAPH)" ]; then \
		echo "Downloading Vosk LGRAPH model (~128 MB)..."; \
		mkdir -p $(MODELDIR); \
		wget -q --show-progress -O $(MODELDIR)/model-lgraph.zip \
			"https://alphacephei.com/vosk/models/vosk-model-en-us-0.22-lgraph.zip"; \
		cd $(MODELDIR) && unzip -q model-lgraph.zip && rm model-lgraph.zip; \
	else \
		echo "Model already exists at $(MODEL_LGRAPH)"; \
	fi

#############################################
# Test All Models
#############################################

test-all-models: all download-models test-small test-lgraph wer-summary-all
	@echo "=== All model tests complete ==="

#############################################
# Small Model Tests (~40 MB)
#############################################

test-small: $(TRANSCRIBER) download-model-small
	@mkdir -p $(OUTPUTDIR)/small
	@echo "=== Testing: SMALL model (~40 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/small/clean.txt -m $(MODEL_SMALL) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/small/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/small/noisy.txt -m $(MODEL_SMALL) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/small/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/small/very_noisy.txt -m $(MODEL_SMALL) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/small/very_noisy_time.txt
	@echo ""

wer-small: $(WER_CALC)
	@echo "=== WER: SMALL model ==="
	@echo -n "Clean: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/small/clean.txt | grep "WER:"
	@echo -n "Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/small/noisy.txt | grep "WER:"
	@echo -n "Very Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/small/very_noisy.txt | grep "WER:"
	@echo ""

#############################################
# LGraph Model Tests (~128 MB)
#############################################

test-lgraph: $(TRANSCRIBER) download-model-lgraph
	@mkdir -p $(OUTPUTDIR)/lgraph
	@echo "=== Testing: LGRAPH model (~128 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/lgraph/clean.txt -m $(MODEL_LGRAPH) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/lgraph/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/lgraph/noisy.txt -m $(MODEL_LGRAPH) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/lgraph/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/lgraph/very_noisy.txt -m $(MODEL_LGRAPH) 2>/dev/null; \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/lgraph/very_noisy_time.txt
	@echo ""

wer-lgraph: $(WER_CALC)
	@echo "=== WER: LGRAPH model ==="
	@echo -n "Clean: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/lgraph/clean.txt | grep "WER:"
	@echo -n "Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/lgraph/noisy.txt | grep "WER:"
	@echo -n "Very Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/lgraph/very_noisy.txt | grep "WER:"
	@echo ""

#############################################
# WER Summary for All Models
#############################################

wer-summary-all: wer-small wer-lgraph
	@echo "============================================"
	@echo "=== Vosk Model Comparison ==="
	@echo "============================================"
