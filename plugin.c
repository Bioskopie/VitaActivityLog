#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <stdint.h>

typedef struct {
    char title_id[12];
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint32_t seconds;
    uint32_t launches;
} DailyRecord;

static int running = 1;
static SceUID thid = -1;
static char current_title[12];

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    for (; i < max - 1 && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < max; i++) dst[i] = '\0';
}

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void update_daily_log(uint32_t add_seconds, uint32_t add_launches) {
    SceDateTime now;
    sceRtcGetCurrentClockLocalTime(&now);
    uint16_t y = sceRtcGetYear(&now);
    uint8_t m = sceRtcGetMonth(&now);
    uint8_t d = sceRtcGetDay(&now);

    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/ActivityLog", 0777);

    const char *path = "ux0:/data/ActivityLog/daily.bin";
    SceUID fd = sceIoOpen(path, SCE_O_RDWR | SCE_O_CREAT, 0777);
    if (fd < 0) return;

    DailyRecord rec;
    SceOff offset = 0;
    int found = 0;

    while (sceIoRead(fd, &rec, sizeof(DailyRecord)) == sizeof(DailyRecord)) {
        if (rec.year == y && rec.month == m && rec.day == d && str_equal(rec.title_id, current_title)) {
            rec.seconds += add_seconds;
            rec.launches += add_launches;
            sceIoLseek(fd, offset, SCE_SEEK_SET);
            sceIoWrite(fd, &rec, sizeof(DailyRecord));
            found = 1;
            break;
        }
        offset += sizeof(DailyRecord);
    }

    if (!found) {
        str_copy(rec.title_id, current_title, 12);
        rec.year = y;
        rec.month = m;
        rec.day = d;
        rec.seconds = add_seconds;
        rec.launches = add_launches;
        sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoWrite(fd, &rec, sizeof(DailyRecord));
    }

    sceIoClose(fd);
}

static int tracker_thread(SceSize args, void *argp) {
    SceRtcTick last_tick, cur_tick;
    sceRtcGetCurrentTick(&last_tick);

    // Log 1 game launch at session start
    update_daily_log(0, 1);

    while (running) {
        sceKernelDelayThread(20 * 1000 * 1000); // Wake once every 20s
        sceRtcGetCurrentTick(&cur_tick);

        uint64_t diff_us = cur_tick.tick - last_tick.tick;
        last_tick = cur_tick;

        // Ignore time if console was suspended in Sleep Mode (> 35 seconds elapsed)
        if (diff_us >= 1000000ULL && diff_us <= 35000000ULL) {
            uint32_t secs = (uint32_t)(diff_us / 1000000ULL);
            update_daily_log(secs, 0);
        }
    }
    return sceKernelExitDeleteThread(0);
}

void _start() __attribute__ ((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
    char titleid[32];
    for (int i = 0; i < 32; i++) titleid[i] = 0;

    sceAppMgrAppParamGetString(0, 12, titleid, 32);

    // Skip system apps (NPXS*) and the Activity Log viewer itself (ACTLOG3DS)
    if ((titleid[0] == 'N' && titleid[1] == 'P' && titleid[2] == 'X' && titleid[3] == 'S') ||
        str_equal(titleid, "ACTLOG3DS") || titleid[0] == '\0') {
        return SCE_KERNEL_START_NO_RESIDENT;
    }

    str_copy(current_title, titleid, 12);
    running = 1;
    thid = sceKernelCreateThread(" actlog_thread", tracker_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    running = 0;
    return SCE_KERNEL_STOP_SUCCESS;
}
