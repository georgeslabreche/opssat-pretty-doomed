/**
 * Standalone test for postcard I/Q scatter rendering.
 * Usage: ./test_postcard_scatter <sc16_file> <frame_jpg> <output_png> [scale]
 */
#include "postcard.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <sc16_file> <frame_jpg> <output_png> [scale]\n", argv[0]);
        return 1;
    }

    PostcardArgs args;
    args.sc16_path = argv[1];
    args.frame_path = argv[2];
    args.output_path = argv[3];
    args.transcription = "TEST POSTCARD I/Q CONSTELLATION SCATTER BUG";
    args.demo_name = "test";
    args.timestamp = "2026-04-03 00:00:00 UTC";
    args.logo_esa = "assets/logo-esa.png";
    args.logo_doom = "assets/logo-doom.png";
    args.logo_pretty = "assets/logo-opssat-pretty.png";
    args.scale = (argc >= 5) ? atoi(argv[4]) : 1;

    printf("Generating postcard with sc16=%s frame=%s scale=%d\n",
           argv[1], argv[2], args.scale);
    bool ok = generate_postcard(args);
    printf("Result: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
