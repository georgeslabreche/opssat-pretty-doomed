#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "g_game.h"
#include "m_random.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

static int ticks = 0;
static int frames = 0;
static int runid = 0;
static char framedir[512] = "";
static int keepgifframes = 0; // -keepgifframes: also write JPEGs for GIF range frames

// Doom log: prefixed with elapsed ms since doom_start
static FILE* doom_log = NULL;
static struct timespec doom_start_time;

static double elapsed_ms(struct timespec* start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000.0
         + (now.tv_nsec - start->tv_nsec) / 1e6;
}

static void doom_log_printf(const char* fmt, ...)
{
    if (!doom_log) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    fprintf(doom_log, "[%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(doom_log, fmt, ap);
    va_end(ap);
}

// Frame capture list: parsed from -frames CLI option.
// Sorted for early-exit lookup in DG_ShouldDrawFrame().
#define MAX_CAPTURE_FRAMES 4096
static int capture_frames[MAX_CAPTURE_FRAMES];
static int capture_count = 0;

// Each dash-range in -frames produces an animated GIF
#define MAX_RANGES 64
typedef struct {
    int lo;
    int hi;
} FrameRange;
static FrameRange gif_ranges[MAX_RANGES];
static int gif_range_count = 0;

// GIF writer state (one active at a time)
static GifWriter gif_writer;
static int gif_active = 0;           // is a GIF currently being written?
static int gif_current_range = -1;   // index into gif_ranges

// DOOM's screen buffer is XRGB (BGRA in memory on little-endian).
// gif.h expects RGBA. This buffer holds the swapped copy.
static uint8_t gif_rgba_buf[DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4];

static void screen_to_rgba(void)
{
    const uint8_t* src = (const uint8_t*)DG_ScreenBuffer;
    int n = DOOMGENERIC_RESX * DOOMGENERIC_RESY;
    for (int i = 0; i < n; i++)
    {
        gif_rgba_buf[i * 4 + 0] = src[i * 4 + 2]; // R <- byte 2
        gif_rgba_buf[i * 4 + 1] = src[i * 4 + 1]; // G <- byte 1
        gif_rgba_buf[i * 4 + 2] = src[i * 4 + 0]; // B <- byte 0
        gif_rgba_buf[i * 4 + 3] = 0xff;            // A
    }
}

static int int_compare(const void* a, const void* b)
{
    return (*(const int*)a) - (*(const int*)b);
}

// Parse -frames value: comma-separated integers and dash ranges.
// Examples: "1920", "400,780", "1918-1922", "400,1918-1922,780"
static void parse_frames(const char* arg)
{
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", arg);

    char* token = strtok(buf, ",");
    while (token != NULL && capture_count < MAX_CAPTURE_FRAMES)
    {
        char* dash = strchr(token, '-');
        if (dash)
        {
            *dash = '\0';
            int lo = atoi(token);
            int hi = atoi(dash + 1);
            for (int f = lo; f <= hi && capture_count < MAX_CAPTURE_FRAMES; f++)
            {
                capture_frames[capture_count++] = f;
            }
            if (gif_range_count < MAX_RANGES)
            {
                gif_ranges[gif_range_count].lo = lo;
                gif_ranges[gif_range_count].hi = hi;
                gif_range_count++;
            }
        }
        else
        {
            capture_frames[capture_count++] = atoi(token);
        }
        token = strtok(NULL, ",");
    }

    // Sort for efficient lookup
    if (capture_count > 0)
    {
        qsort(capture_frames, capture_count, sizeof(int), int_compare);
    }
}

static void doom_log_close(void)
{
    if (doom_log)
    {
        doom_log_printf("doom_end\n");
        fclose(doom_log);
        doom_log = NULL;
    }
}

void DG_Init()
{
    int p;
    p = M_CheckParmWithArgs("-runid", 1);
    if (p)
    {
        runid = atoi(myargv[p + 1]);
    }

    p = M_CheckParmWithArgs("-framedir", 1);
    if (p)
    {
        snprintf(framedir, sizeof(framedir), "%s", myargv[p + 1]);
        mkdir(framedir, 0755);
    }
    else
    {
        // Legacy behavior: create toGround/run-NNNNNN relative to CWD
        mkdir("toGround", 0755);
        snprintf(framedir, sizeof(framedir), "toGround/run-%06d", runid);
        int result = mkdir(framedir, 0755);
        if (result == -1 && errno != EEXIST)
        {
            printf("Error creating directory (%d): %s\n", errno, framedir);
            exit(1);
        }
    }

    p = M_CheckParmWithArgs("-frames", 1);
    if (p)
    {
        parse_frames(myargv[p + 1]);
    }

    if (M_CheckParm("-keepgifframes"))
    {
        keepgifframes = 1;
    }

    // Open doom log
    char logpath[600];
    snprintf(logpath, sizeof(logpath), "%s/doom.log", framedir);
    doom_log = fopen(logpath, "w");
    clock_gettime(CLOCK_MONOTONIC, &doom_start_time);
    if (doom_log)
    {
        doom_log_printf("doom_start\n");
        fflush(doom_log);
    }
    atexit(doom_log_close);
}

void WriteFrameToDisk(int id, int frameid)
{
    char filename[600];
    snprintf(filename, sizeof(filename), "%s/frame-%06d.jpg", framedir, frameid);
    stbi_write_jpg(filename, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 4, DG_ScreenBuffer, 100);
}

static int frame_in_gif_range(int frame)
{
    for (int i = 0; i < gif_range_count; i++)
    {
        if (frame >= gif_ranges[i].lo && frame <= gif_ranges[i].hi)
            return 1;
    }
    return 0;
}

int DG_ShouldDrawFrame()
{
    frames++;
    for (int i = 0; i < capture_count; i++)
    {
        if (capture_frames[i] == frames) return 1;
        if (capture_frames[i] > frames) break;
    }
    return 0;
}

void DG_DrawFrame()
{
    // DG_DrawFrame can be called multiple times per tick (e.g. screen wipes).
    // Only capture once per unique frame number.
    static int last_drawn_frame = -1;
    if (frames == last_drawn_frame) return;
    last_drawn_frame = frames;

    struct timespec t0;
    int in_gif_range = frame_in_gif_range(frames);

    // Write JPEG (skip for GIF range frames unless -keepgifframes)
    if (!in_gif_range || keepgifframes)
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        WriteFrameToDisk(runid, frames);
        doom_log_printf("jpeg_write frame=%d time_ms=%.3f\n", frames, elapsed_ms(&t0));
    }

    // Check if current frame belongs to any GIF range
    for (int i = 0; i < gif_range_count; i++)
    {
        if (frames < gif_ranges[i].lo || frames > gif_ranges[i].hi)
            continue;

        if (frames == gif_ranges[i].lo)
        {
            // Start a new GIF
            char gif_filename[600];
            snprintf(gif_filename, sizeof(gif_filename),
                     "%s/frames-%06d-%06d.gif", framedir, gif_ranges[i].lo, gif_ranges[i].hi);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            GifBegin(&gif_writer, gif_filename, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 3, 8, false);
            doom_log_printf("gif_begin range=%d-%d time_ms=%.3f\n",
                            gif_ranges[i].lo, gif_ranges[i].hi, elapsed_ms(&t0));
            gif_active = 1;
            gif_current_range = i;
        }

        if (gif_active && gif_current_range == i)
        {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            screen_to_rgba();
            GifWriteFrame(&gif_writer, gif_rgba_buf,
                          DOOMGENERIC_RESX, DOOMGENERIC_RESY, 3, 8, false);
            doom_log_printf("gif_frame frame=%d time_ms=%.3f\n", frames, elapsed_ms(&t0));
        }

        if (frames == gif_ranges[i].hi && gif_active && gif_current_range == i)
        {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            GifEnd(&gif_writer);
            doom_log_printf("gif_end range=%d-%d time_ms=%.3f\n",
                            gif_ranges[i].lo, gif_ranges[i].hi, elapsed_ms(&t0));
            gif_active = 0;
            gif_current_range = -1;
        }

        break;
    }

    if (doom_log) fflush(doom_log);
}

void DG_SleepMs(uint32_t ms)
{
}

uint32_t DG_GetTicksMs()
{
    ticks += 30;
    return ticks;
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
    return 0;
}

void DG_SetWindowTitle(const char * title)
{
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }

    return 0;
}
