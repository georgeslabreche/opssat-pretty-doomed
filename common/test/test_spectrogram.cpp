// Standalone test for pretty_spectrogram.h
// Usage: test_spectrogram <input.sc16> <sample_rate_hz>
#include "pretty_log.h"
#include "pretty_spectrogram.h"

using namespace pretty;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        log_error() << "Usage: " << argv[0] << " <input.sc16> <sample_rate_hz>\n";
        return 1;
    }

    std::string sc16_path = argv[1];
    long long sample_rate = std::stoll(argv[2]);

    std::string out_path = make_spectrogram_filename(sc16_path);
    log_info() << "Input:  " << sc16_path << "\n";
    log_info() << "Output: " << out_path << "\n";
    log_info() << "Sample rate: " << sample_rate << " Hz\n";

    if (!generate_spectrogram(sc16_path, out_path, sample_rate)) {
        log_error() << "Spectrogram generation failed\n";
        return 1;
    }

    log_info() << "Done: " << out_path << "\n";
    return 0;
}
