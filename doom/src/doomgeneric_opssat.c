#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "g_game.h"
#include "m_random.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

static int ticks = 0;
static int frames = 0;
static int runid = 0;
static char framedir[512] = "";

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
}

void WriteFrameToDisk(int id, int frameid)
{
    char filename[600];
    snprintf(filename, sizeof(filename), "%s/frame-%06d.jpg", framedir, frameid);
    stbi_write_jpg(filename, DOOMGENERIC_RESX, DOOMGENERIC_RESY, 4, DG_ScreenBuffer, 100);
}

int DG_ShouldDrawFrame()
{
    frames++;
    return (runid == 1 && frames == 1920)
        || (runid == 2 && frames == 780)
        || (runid == 3 && frames == 1911)
        || (runid == 4 && frames == 2113)
        || (runid == 5 && frames == 1624)
        || (runid == 6 && frames == 1234)
        || (runid == 7 && frames == 6560)
        || (runid == 8 && frames == 24695)
        || (runid == 9 && frames == 2320);
}

void DG_DrawFrame()
{
    WriteFrameToDisk(runid, frames);
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