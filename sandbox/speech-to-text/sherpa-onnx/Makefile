CC = gcc
CFLAGS = -Wall -O2 -std=c99 -I$(SHERPA_ONNX_DIR)/include
LDFLAGS = -L$(SHERPA_ONNX_DIR)/lib -Wl,-rpath,$(SHERPA_ONNX_DIR)/lib
LIBS = -lsherpa-onnx-c-api -lsndfile -lm -lpthread

SHERPA_ONNX_DIR ?= /opt/sherpa-onnx

SRCDIR = src
BUILDDIR = build
OUTPUTDIR = output
MODELDIR = models

TRANSCRIBER = $(BUILDDIR)/sherpa_transcriber
WER_CALC = $(BUILDDIR)/wer_calculator

# Sample paths
GEORGES_CLEAN = /samples/georges/georges_opssat_clean.wav
GEORGES_NOISY = /samples/georges/georges_opssat_noisy.wav
GEORGES_VERY_NOISY = /samples/georges/georges_opssat_very_noisy.wav
OPSSAT_SDR = /samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav

# Model paths
MODEL_SMALL = $(MODELDIR)/sherpa-onnx-zipformer-small-en-2023-06-26
MODEL_BASE = $(MODELDIR)/sherpa-onnx-zipformer-en-2023-06-26
MODEL_LARGE = $(MODELDIR)/sherpa-onnx-zipformer-large-en-2023-06-26

# Ground truth (mounted from samples directory)
GROUND_TRUTH = /samples/georges/georges_opssat_transcript.txt

.PHONY: all clean download-models download-model-small download-model-base download-model-large \
        test-all test-all-models wer-summary-all

all: $(TRANSCRIBER) $(WER_CALC)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OUTPUTDIR):
	mkdir -p $(OUTPUTDIR)

$(TRANSCRIBER): $(SRCDIR)/sherpa_transcriber.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

$(WER_CALC): $(SRCDIR)/wer_calculator.c | $(BUILDDIR)
	$(CC) -Wall -O2 -std=c99 $< -o $@

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OUTPUTDIR)

#############################################
# Model Downloads
#############################################

download-models: download-model-small download-model-base download-model-large

download-model-small:
	@if [ ! -d "$(MODEL_SMALL)" ]; then \
		echo "Downloading Sherpa-ONNX SMALL model (~27 MB)..."; \
		mkdir -p $(MODELDIR); \
		wget -q --show-progress -O $(MODELDIR)/model-small.tar.bz2 \
			"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-zipformer-small-en-2023-06-26.tar.bz2"; \
		cd $(MODELDIR) && tar --no-same-owner -xjf model-small.tar.bz2 && rm model-small.tar.bz2; \
	else \
		echo "Model already exists at $(MODEL_SMALL)"; \
	fi

download-model-base:
	@if [ ! -d "$(MODEL_BASE)" ]; then \
		echo "Downloading Sherpa-ONNX BASE model (~68 MB)..."; \
		mkdir -p $(MODELDIR); \
		wget -q --show-progress -O $(MODELDIR)/model-base.tar.bz2 \
			"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-zipformer-en-2023-06-26.tar.bz2"; \
		cd $(MODELDIR) && tar --no-same-owner -xjf model-base.tar.bz2 && rm model-base.tar.bz2; \
	else \
		echo "Model already exists at $(MODEL_BASE)"; \
	fi

download-model-large:
	@if [ ! -d "$(MODEL_LARGE)" ]; then \
		echo "Downloading Sherpa-ONNX LARGE model (~147 MB)..."; \
		mkdir -p $(MODELDIR); \
		wget -q --show-progress -O $(MODELDIR)/model-large.tar.bz2 \
			"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-zipformer-large-en-2023-06-26.tar.bz2"; \
		cd $(MODELDIR) && tar --no-same-owner -xjf model-large.tar.bz2 && rm model-large.tar.bz2; \
	else \
		echo "Model already exists at $(MODEL_LARGE)"; \
	fi

#############################################
# Test All Models
#############################################

test-all-models: all download-models test-small test-base test-large wer-summary-all
	@echo "=== All model tests complete ==="

#############################################
# Small Model Tests (~27 MB)
#############################################

test-small: $(TRANSCRIBER) download-model-small
	@mkdir -p $(OUTPUTDIR)/small
	@echo "=== Testing: SMALL model (~27 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/small/clean.txt -m $(MODEL_SMALL); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/small/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/small/noisy.txt -m $(MODEL_SMALL); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/small/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/small/very_noisy.txt -m $(MODEL_SMALL); \
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
# Base Model Tests (~68 MB)
#############################################

test-base: $(TRANSCRIBER) download-model-base
	@mkdir -p $(OUTPUTDIR)/base
	@echo "=== Testing: BASE model (~68 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/base/clean.txt -m $(MODEL_BASE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/base/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/base/noisy.txt -m $(MODEL_BASE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/base/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/base/very_noisy.txt -m $(MODEL_BASE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/base/very_noisy_time.txt
	@echo ""

wer-base: $(WER_CALC)
	@echo "=== WER: BASE model ==="
	@echo -n "Clean: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/base/clean.txt | grep "WER:"
	@echo -n "Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/base/noisy.txt | grep "WER:"
	@echo -n "Very Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/base/very_noisy.txt | grep "WER:"
	@echo ""

#############################################
# Large Model Tests (~147 MB)
#############################################

test-large: $(TRANSCRIBER) download-model-large
	@mkdir -p $(OUTPUTDIR)/large
	@echo "=== Testing: LARGE model (~147 MB) ==="
	@echo "--- Clean ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_CLEAN) -o $(OUTPUTDIR)/large/clean.txt -m $(MODEL_LARGE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/large/clean_time.txt
	@echo "--- Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_NOISY) -o $(OUTPUTDIR)/large/noisy.txt -m $(MODEL_LARGE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/large/noisy_time.txt
	@echo "--- Very Noisy ---"
	@START=$$(date +%s.%N); \
	./$(TRANSCRIBER) -i $(GEORGES_VERY_NOISY) -o $(OUTPUTDIR)/large/very_noisy.txt -m $(MODEL_LARGE); \
	END=$$(date +%s.%N); \
	echo "$$END $$START" | awk '{printf "Time: %.2fs\n", $$1-$$2}' | tee $(OUTPUTDIR)/large/very_noisy_time.txt
	@echo ""

wer-large: $(WER_CALC)
	@echo "=== WER: LARGE model ==="
	@echo -n "Clean: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/large/clean.txt | grep "WER:"
	@echo -n "Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/large/noisy.txt | grep "WER:"
	@echo -n "Very Noisy: "; ./$(WER_CALC) $(GROUND_TRUTH) $(OUTPUTDIR)/large/very_noisy.txt | grep "WER:"
	@echo ""

#############################################
# WER Summary for All Models
#############################################

wer-summary-all: wer-small wer-base wer-large
	@echo "============================================"
	@echo "=== Sherpa-ONNX Model Comparison ==="
	@echo "============================================"
