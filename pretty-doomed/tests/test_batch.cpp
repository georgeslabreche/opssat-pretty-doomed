#include "doctest.h"
#include "batch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
using Windows = std::vector<std::pair<std::size_t, std::size_t>>;
constexpr int RATE = 16000;
constexpr std::size_t MIN = 8000;
}

TEST_CASE("stt_windows without a window length gives one window over all samples") {
    CHECK(stt_windows(100000, RATE, 0.0, MIN) == Windows{{0, 100000}});
}

TEST_CASE("stt_windows cuts the audio into windows of the given length") {
    CHECK(stt_windows(2 * 30 * RATE, RATE, 30.0, MIN) ==
          Windows{{0, 30 * RATE}, {30 * RATE, 60 * RATE}});
}

TEST_CASE("stt_windows keeps a last window that is at least the minimum length") {
    std::size_t n = 30 * RATE + MIN;
    CHECK(stt_windows(n, RATE, 30.0, MIN) == Windows{{0, 30 * RATE}, {30 * RATE, n}});
}

TEST_CASE("stt_windows drops a last window that is shorter than the minimum length") {
    CHECK(stt_windows(30 * RATE + MIN - 1, RATE, 30.0, MIN) == Windows{{0, 30 * RATE}});
}

TEST_CASE("stt_windows keeps a short audio as its only window") {
    CHECK(stt_windows(MIN - 1, RATE, 30.0, MIN) == Windows{{0, MIN - 1}});
}

TEST_CASE("stt_windows gives no window for no samples") {
    CHECK(stt_windows(0, RATE, 30.0, MIN).empty());
}

TEST_CASE("join_transcripts joins with one space and skips empty parts") {
    CHECK(join_transcripts({"MAYDAY MAYDAY", "", "TAKING ON WATER"}) == "MAYDAY MAYDAY TAKING ON WATER");
    CHECK(join_transcripts({"", ""}).empty());
}

TEST_CASE("list_wav_files gives the wav files of a directory sorted by name") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pretty_test_list_wav_files";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub.wav");
    for (const char* name : {"b.wav", "a.wav", "c.txt", "d.WAV.bak"}) {
        std::ofstream(dir / name) << "x";
    }
    auto files = list_wav_files(dir.string());
    REQUIRE(files.size() == 2);
    CHECK(file_stem(files[0]) == "a");
    CHECK(file_stem(files[1]) == "b");
    fs::remove_all(dir);
}

TEST_CASE("list_wav_files gives nothing for a path that is not a directory") {
    CHECK(list_wav_files("/nonexistent/pretty").empty());
}

TEST_CASE("file_stem removes the directory and the extension") {
    CHECK(file_stem("/a/b/41096-kupreanof.wav") == "41096-kupreanof");
}

TEST_CASE("parse_vmhwm_kb reads the VmHWM line") {
    std::istringstream status("Name:\tpretty-doomed\nVmPeak:\t  900000 kB\nVmHWM:\t  612345 kB\nVmRSS:\t  500000 kB\n");
    CHECK(parse_vmhwm_kb(status) == 612345);
}

TEST_CASE("parse_vmhwm_kb gives -1 without a VmHWM line") {
    std::istringstream status("Name:\tpretty-doomed\nVmRSS:\t  500000 kB\n");
    CHECK(parse_vmhwm_kb(status) == -1);
}

TEST_CASE("parse_status_kb reads the line of the given key, not a longer key with the same start") {
    std::istringstream status("Name:\tpretty-doomed\nVmRSSx:\t  1 kB\nVmHWM:\t  612345 kB\nVmRSS:\t  500000 kB\n");
    CHECK(parse_status_kb(status, "VmRSS") == 500000);
}

TEST_CASE("parse_status_kb gives -1 without the key") {
    std::istringstream status("Name:\tpretty-doomed\n");
    CHECK(parse_status_kb(status, "VmRSS") == -1);
}
