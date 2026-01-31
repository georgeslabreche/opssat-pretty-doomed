#include "transcriber.h"
#include <iostream>
#include <cstring>
#include "sherpa-onnx/c-api/c-api.h"

std::string transcribe(const std::vector<float>& samples,
                       int sample_rate,
                       const PipelineConfig& cfg) {
    const std::string& encoder_path = cfg.model_encoder;
    const std::string& decoder_path = cfg.model_decoder;
    const std::string& joiner_path  = cfg.model_joiner;
    const std::string& tokens_path  = cfg.model_tokens;

    // Configure recognizer
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    config.model_config.transducer.encoder = encoder_path.c_str();
    config.model_config.transducer.decoder = decoder_path.c_str();
    config.model_config.transducer.joiner  = joiner_path.c_str();
    config.model_config.tokens = tokens_path.c_str();
    config.model_config.num_threads = cfg.num_threads;
    config.model_config.debug = 0;
    config.decoding_method = cfg.decoding_method.c_str();

    // Create recognizer
    const SherpaOnnxOfflineRecognizer* recognizer =
        SherpaOnnxCreateOfflineRecognizer(&config);

    if (!recognizer) {
        std::cerr << "Error: Cannot create recognizer. Check model paths:" << std::endl;
        std::cerr << "  Encoder: " << encoder_path << std::endl;
        std::cerr << "  Decoder: " << decoder_path << std::endl;
        std::cerr << "  Joiner:  " << joiner_path << std::endl;
        std::cerr << "  Tokens:  " << tokens_path << std::endl;
        return "";
    }

    // Create stream and feed audio
    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        std::cerr << "Error: Cannot create stream" << std::endl;
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        return "";
    }

    SherpaOnnxAcceptWaveformOffline(stream, sample_rate, samples.data(), samples.size());

    // Decode
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    // Get result
    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream);

    std::string transcription = (result && result->text) ? result->text : "";

    // Cleanup
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);
    SherpaOnnxDestroyOfflineRecognizer(recognizer);

    return transcription;
}
