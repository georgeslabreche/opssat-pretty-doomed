#include "doctest.h"
#include "executor.h"
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

static std::string make_temp_dir() {
    char tmpl[] = "/tmp/test_executor_XXXXXX";
    char* dir = mkdtemp(tmpl);
    return std::string(dir);
}

static void touch(const std::string& path) {
    std::ofstream f(path);
}

static void rm_rf(const std::string& dir) {
    std::string cmd = "rm -rf " + dir;
    system(cmd.c_str());
}

TEST_CASE("find_demo_files returns sorted .lmp basenames") {
    std::string dir = make_temp_dir();
    touch(dir + "/e1m7-607.lmp");
    touch(dir + "/impfight.lmp");
    touch(dir + "/m1-fast.lmp");
    touch(dir + "/doom.wad");       // not a .lmp
    touch(dir + "/readme.txt");     // not a .lmp

    auto demos = find_demo_files(dir);
    REQUIRE(demos.size() == 3);
    CHECK(demos[0] == "e1m7-607");
    CHECK(demos[1] == "impfight");
    CHECK(demos[2] == "m1-fast");

    rm_rf(dir);
}

TEST_CASE("find_demo_files empty directory") {
    std::string dir = make_temp_dir();

    auto demos = find_demo_files(dir);
    CHECK(demos.empty());

    rm_rf(dir);
}

TEST_CASE("find_demo_files skips hidden files") {
    std::string dir = make_temp_dir();
    touch(dir + "/.hidden.lmp");
    touch(dir + "/visible.lmp");

    auto demos = find_demo_files(dir);
    REQUIRE(demos.size() == 1);
    CHECK(demos[0] == "visible");

    rm_rf(dir);
}

TEST_CASE("find_demo_files nonexistent directory") {
    auto demos = find_demo_files("/tmp/nonexistent_dir_12345");
    CHECK(demos.empty());
}

TEST_CASE("resolve_frames single -1 returns random frame") {
    std::unordered_map<std::string, int> maxframes = {{"test", 1000}};
    std::string result = resolve_frames("-1", "test", 0, maxframes);
    int frame = std::stoi(result);
    CHECK(frame >= 100);
    CHECK(frame <= 950);
}

TEST_CASE("resolve_frames cycling integers") {
    std::unordered_map<std::string, int> maxframes;
    CHECK(resolve_frames("400,300,500", "test", 0, maxframes) == "400");
    CHECK(resolve_frames("400,300,500", "test", 1, maxframes) == "300");
    CHECK(resolve_frames("400,300,500", "test", 2, maxframes) == "500");
    CHECK(resolve_frames("400,300,500", "test", 3, maxframes) == "400"); // wraps
}

TEST_CASE("resolve_frames cycling with dash ranges") {
    std::unordered_map<std::string, int> maxframes = {{"test", 4096}};
    CHECK(resolve_frames("646-675,2324-2353,-1", "test", 0, maxframes) == "646-675");
    CHECK(resolve_frames("646-675,2324-2353,-1", "test", 1, maxframes) == "2324-2353");
    // run_cycle 2 picks "-1" which resolves to a random frame
    std::string random = resolve_frames("646-675,2324-2353,-1", "test", 2, maxframes);
    int frame = std::stoi(random);
    CHECK(frame >= 100);
    CHECK(frame <= 4046);
}

TEST_CASE("resolve_frames single range passes through") {
    std::unordered_map<std::string, int> maxframes;
    CHECK(resolve_frames("7992-8025", "test", 0, maxframes) == "7992-8025");
}

TEST_CASE("resolve_frames single number passes through") {
    std::unordered_map<std::string, int> maxframes;
    CHECK(resolve_frames("500", "test", 0, maxframes) == "500");
}
