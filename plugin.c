#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
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

// Reads TITLE_ID directly from the running game's mounted app0:sce_sys/param.sfo
static int get_running_title_id(char *out_tid) {
    SceUID fd = sceIoOpen("app0:sce_sys/param.sfo", SCE_O_RDONLY, 0);
    if (fd < 0) fd = sceIoOpen("app0:/sce_sys/param.sfo", SCE_O_RDONLY, 0);
    if (fd < 0) return 0;

    uint8_t buf[2048];
    int read = sceIoRead(fd, buf, sizeof(buf));
    sceIoClose(fd);
    if (read < 20) return 0;

    uint32_t magic = *(uint32_t *)(buf + 0);
    if (magic != 0x46535000) return 0; // "\0PSF"

    uint32_t key_off  = *(uint32_t *)(buf + 8);
    uint32_t data_off = *(uint32_t *)(buf + 12);
    uint32_t count    = *(uint32_t *)(buf + 16);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry_pos = 20 + i * 16;
        if (entry_pos + 16 > (uint32_t)read) break;

        uint16_t k_off = *(uint16_t *)(buf + entry_pos + 0);
        uint32_t d_off = *(uint32_t *)(buf + entry_pos + 12);

        if (key_off + k_off < (uint32_t)read && data_off + d_off < (uint32_t)read) {
            const char *key = (const char *)(buf + key_off + k_off);
            if (str_equal(key, "TITLE_ID")) {
                str_copy(out_tid, (const char *)(buf + data_off + d_off), 12);
                return out_tid[0] != '\0';
            }
        }
    }
    return 0;
}

static int write_to_path(const char *path, uint16_t y, uint8_t m, uint8_t d, uint32_t add_seconds, uint32_t add_launches) {
    SceUID fd = sceIoOpen(path, SCE_O_RDWR | SCE_O_CREAT, 0777);
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

    // Primary location (same folder TrackPlug uses on PS Vita!)
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/ActivityLog", 0777);
    if (write_to_path("ux0:data/ActivityLog/daily.bin", y, m, d, add_seconds, add_launches) < 0) {
        // Fallback if sandboxed
        write_to_path("ur0:temp/actlog_daily.bin", y, m, d, add_seconds, add_launches);
    }
}

static int tracker_thread(SceSize args, void *argp) {
    // Wait 2 seconds for app0: mount to stabilize
    sceKernelDelayThread(2 * 1000 * 1000);

    char titleid[16];
    for (int i = 0; i < 16; i++) titleid[i] = '\0';

    if (!get_running_title_id(titleid)) {
        return sceKernelExitDeleteThread(0);
    }

    // Do not track system apps, VitaShell, or the Activity Log viewer itself
    if ((titleid[0] == 'N' && titleid[1] == 'P' && titleid[2] == 'X' && titleid[3] == 'S') ||
        str_equal(titleid, "ACTLOG3DS") || str_equal(titleid, "VITASHELL")) {
        return sceKernelExitDeleteThread(0);
    }

    str_copy(current_title, titleid, 12);

    SceRtcTick last_tick, cur_tick;
    sceRtcGetCurrentTick(&last_tick);

    // Log 1 game launch immediately
    update_daily_log(0, 1);

    while (running) {
        sceKernelDelayThread(5 * 1000 * 1000); // Save every 5 seconds
        sceRtcGetCurrentTick(&cur_tick);

        uint64_t diff_us = cur_tick.tick - last_tick.tick;
        last_tick = cur_tick;

        // Ignore time if console was suspended in Sleep Mode (> 15s gap)
        if (diff_us >= 1000000ULL && diff_us <= 15000000ULL) {
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
