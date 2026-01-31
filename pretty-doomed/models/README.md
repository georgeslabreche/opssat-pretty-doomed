# Models

ML models used by the PRETTY DOOMed pipeline (e.g. speech-to-text, denoising). All model files belong in this directory. The `.onnx` files are gitignored due to size.

## sherpa-onnx/small

[sherpa-onnx-zipformer-small-en-2023-06-26](https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26) (int8 quantized, ~27 MB). Supports `greedy_search` and `modified_beam_search` decoding methods.

The `.onnx` files are gitignored due to size. Download:

```bash
git lfs install
git clone https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26 /tmp/sherpa-model
mkdir -p models/sherpa-onnx/small
cp /tmp/sherpa-model/{encoder-epoch-99-avg-1.int8.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.int8.onnx,tokens.txt} models/sherpa-onnx/small/
```

Model paths are configured in `config.cfg`.
