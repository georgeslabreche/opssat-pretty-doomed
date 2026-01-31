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
