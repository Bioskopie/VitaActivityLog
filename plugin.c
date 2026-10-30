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

static int write_to_path(const char *path, uint16_t y, uint8_t m, uint8_t d, uint32_t add_seconds, uint32_t add_launches) {
    SceUID fd = sceIoOpen(path, SCE_O_RDWR | SCE_O_CREAT, 0666);
    if (fd < 0) return -1;

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
    return 0;
}

static void update_daily_log(uint32_t add_seconds, uint32_t add_launches) {
    SceDateTime now;
    sceRtcGetCurrentClockLocalTime(&now);
    uint16_t y = sceRtcGetYear(&now);
    uint8_t m = sceRtcGetMonth(&now);
    uint8_t d = sceRtcGetDay(&now);

    // ur0:/temp is accessible by sandboxed retail games without kernel plugin
    sceIoMkdir("ur0:/temp", 0777);
    if (write_to_path("ur0:/temp/actlog_daily.bin", y, m, d, add_seconds, add_launches) < 0) {
        sceIoMkdir("ux0:/data", 0777);
        sceIoMkdir("ux0:/data/ActivityLog", 0777);
        write_to_path("ux0:/data/ActivityLog/daily.bin", y, m, d, add_seconds, add_launches);
    }
}

static int tracker_thread(SceSize args, void *argp) {
    // Wait 3 seconds for game initialization to finish
    sceKernelDelayThread(3 * 1000 * 1000);

    // Grab Title ID inside the thread so it is guaranteed to be populated
    char titleid[32];
    for (int i = 0; i < 32; i++) titleid[i] = '\0';
    sceAppMgrAppParamGetString(0, 12, titleid, 32);

    if (titleid[0] == '\0' ||
        (titleid[0] == 'N' && titleid[1] == 'P' && titleid[2] == 'X' && titleid[3] == 'S') ||
        str_equal(titleid, "ACTLOG3DS") || str_equal(titleid, "VITASHELL")) {
        return sceKernelExitDeleteThread(0);
    }

    str_copy(current_title, titleid, 12);

    SceRtcTick last_tick, cur_tick;
    sceRtcGetCurrentTick(&last_tick);

    // Log 1 launch
    update_daily_log(0, 1);

    while (running) {
        sceKernelDelayThread(10 * 1000 * 1000); // Update every 10 seconds
        sceRtcGetCurrentTick(&cur_tick);

        uint64_t diff_us = cur_tick.tick - last_tick.tick;
        last_tick = cur_tick;

        // Ignore time if console was suspended in Sleep Mode (> 25s gap)
        if (diff_us >= 1000000ULL && diff_us <= 25000000ULL) {
            uint32_t secs = (uint32_t)diff_us / 1000000U;
            update_daily_log(secs, 0);
        }
    }
    return sceKernelExitDeleteThread(0);
}

int module_start(SceSize argc, const void *args);
int _start(SceSize argc, const void *args) __attribute__ ((weak, alias("module_start")));

int module_start(SceSize argc, const void *args) {
    running = 1;
    thid = sceKernelCreateThread("actlog_thread", tracker_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    running = 0;
    return SCE_KERNEL_STOP_SUCCESS;
}
