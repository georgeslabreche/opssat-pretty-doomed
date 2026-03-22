#include "doctest.h"
#include "postcard.h"
#include "executor.h"
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>

static std::string make_temp_dir() {
    char tmpl[] = "/tmp/test_postcard_XXXXXX";
    char* dir = mkdtemp(tmpl);
    return std::string(dir);
}

static void rm_rf(const std::string& dir) {
    std::string cmd = "rm -rf " + dir;
    system(cmd.c_str());
}

TEST_CASE("generate_postcard returns false with missing frame") {
    std::string dir = make_temp_dir();

    PostcardArgs args;
    args.frame_path = dir + "/nonexistent.jpg";
    args.output_path = dir + "/postcard.png";
    args.transcription = "TEST";
    args.demo_name = "test";
    args.scale = 1;

    CHECK_FALSE(generate_postcard(args));

    rm_rf(dir);
}

TEST_CASE("generate_postcard handles empty transcription") {
    std::string dir = make_temp_dir();

    // Create a minimal 1x1 PPM as frame (stb_image can load PPM)
    std::string frame_path = dir + "/frame.ppm";
    {
        std::ofstream f(frame_path, std::ios::binary);
        f << "P6\n1 1\n255\n";
        f.put((char)255); f.put((char)0); f.put((char)0); // red pixel
    }

    PostcardArgs args;
    args.frame_path = frame_path;
    args.output_path = dir + "/postcard.png";
    args.transcription = "";
    args.demo_name = "test";
    args.scale = 1;

    CHECK(generate_postcard(args));

    // Verify output file exists
    struct stat st;
    CHECK(stat(args.output_path.c_str(), &st) == 0);
    CHECK(st.st_size > 0);

    rm_rf(dir);
}

TEST_CASE("generate_postcard with text and no sc16") {
    std::string dir = make_temp_dir();

    std::string frame_path = dir + "/frame.ppm";
    {
        std::ofstream f(frame_path, std::ios::binary);
        f << "P6\n2 2\n255\n";
        for (int i = 0; i < 4; i++) {
            f.put((char)200); f.put((char)50); f.put((char)50);
        }
    }

    PostcardArgs args;
    args.frame_path = frame_path;
    args.output_path = dir + "/postcard.png";
    args.transcription = "PRETTY PLAY DOOM";
    args.demo_name = "impfight";
    args.timestamp = "2026-03-22 12:00:00 UTC";
    args.scale = 1;
    // sc16_path empty: scatter and spectrogram skipped

    CHECK(generate_postcard(args));

    struct stat st;
    CHECK(stat(args.output_path.c_str(), &st) == 0);
    CHECK(st.st_size > 100); // should be a real PNG

    rm_rf(dir);
}

TEST_CASE("find_doom_frame prefers GIF over JPG") {
    std::string dir = make_temp_dir();

    // Create dummy files
    { std::ofstream f(dir + "/frame-000100.jpg"); f << "jpg"; }
    { std::ofstream f(dir + "/frames-005000-005020.gif"); f << "gif"; }

    std::string found = find_doom_frame(dir);
    CHECK(found.find(".gif") != std::string::npos);

    rm_rf(dir);
}

TEST_CASE("find_doom_frame returns JPG when no GIF") {
    std::string dir = make_temp_dir();

    { std::ofstream f(dir + "/frame-000100.jpg"); f << "jpg"; }

    std::string found = find_doom_frame(dir);
    CHECK(found.find(".jpg") != std::string::npos);

    rm_rf(dir);
}

TEST_CASE("find_doom_frame returns empty for empty dir") {
    std::string dir = make_temp_dir();

    std::string found = find_doom_frame(dir);
    CHECK(found.empty());

    rm_rf(dir);
}

TEST_CASE("find_doom_frame returns empty for nonexistent dir") {
    std::string found = find_doom_frame("/tmp/nonexistent_doom_dir_12345");
    CHECK(found.empty());
}
