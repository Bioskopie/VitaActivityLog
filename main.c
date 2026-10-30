#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define MAX_GAMES 256
#define MAX_DAILY_RECORDS 8192

typedef struct {
    char title_id[12];
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint32_t seconds;
    uint32_t launches;
} DailyRecord;

typedef struct {
    char title_id[12];
    uint32_t seconds;
} SnapshotRecord;

typedef struct {
    char title_id[12];
    char name[128];
    uint32_t base_seconds;
    uint32_t logged_seconds;
    uint32_t period_seconds;
    uint32_t total_launches;
    uint16_t first_year, last_year;
    uint8_t first_month, first_day, last_month, last_day;
    vita2d_texture *icon;
} GameEntry;

typedef enum {
    VIEW_DAILY_RECORDS = 0,
    VIEW_LIBRARY_RANKINGS,
    VIEW_GAME_DETAILS
} AppState;

typedef enum {
    FILTER_DAY = 0,
    FILTER_WEEK,
    FILTER_MONTH,
    FILTER_YEAR,
    FILTER_ALL_TIME
} TimeFilter;

static GameEntry games[MAX_GAMES];
static int game_count = 0;
static DailyRecord *daily_logs = NULL;
static int daily_count = 0;

static int dark_mode = 0;
static uint32_t BTN_CONFIRM = SCE_CTRL_CROSS;
static uint32_t BTN_CANCEL  = SCE_CTRL_CIRCLE;
static const char *STR_CONFIRM = "X";
static const char *STR_CANCEL  = "O";

static int sel_year = 2026, sel_month = 1, sel_day = 1;

static int parse_sfo_title(const char *path, char *out_title, size_t max_len) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;

    uint8_t buf[4096];
    int read = sceIoRead(fd, buf, sizeof(buf));
    sceIoClose(fd);
    if (read < 20) return 0;

    uint32_t magic = *(uint32_t *)(buf + 0);
    if (magic != 0x46535000) return 0;

    uint32_t key_off  = *(uint32_t *)(buf + 8);
    uint32_t data_off = *(uint32_t *)(buf + 12);
    uint32_t count    = *(uint32_t *)(buf + 16);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry_pos = 20 + i * 16;
        if (entry_pos + 16 > (uint32_t)read) break;

        uint16_t k_off = *(uint16_t *)(buf + entry_pos + 0);
        uint32_t d_len = *(uint32_t *)(buf + entry_pos + 4);
        uint32_t d_off = *(uint32_t *)(buf + entry_pos + 12);

        if (key_off + k_off < (uint32_t)read && data_off + d_off < (uint32_t)read) {
            const char *key = (const char *)(buf + key_off + k_off);
            if (strcmp(key, "TITLE") == 0 || strcmp(key, "STITLE") == 0) {
                size_t copy_len = (d_len < max_len - 1) ? d_len : (max_len - 1);
                memcpy(out_title, buf + data_off + d_off, copy_len);
                out_title[copy_len] = '\0';
                for (size_t c = 0; out_title[c] != '\0'; c++) {
                    if (out_title[c] == '\n' || out_title[c] == '\r') out_title[c] = ' ';
                }
                if (strcmp(key, "TITLE") == 0) return 1;
            }
        }
    }
    return out_title[0] != '\0';
}

static int find_or_add_game(const char *title_id) {
    if (strncmp(title_id, "NPXS", 4) == 0 || strcmp(title_id, "ACTLOG3DS") == 0 || strcmp(title_id, "VITASHELL") == 0) return -1;
    for (int i = 0; i < game_count; i++) {
        if (strcmp(games[i].title_id, title_id) == 0) return i;
    }
    if (game_count >= MAX_GAMES) return -1;

    int idx = game_count++;
    memset(&games[idx], 0, sizeof(GameEntry));
    strncpy(games[idx].title_id, title_id, 11);
    strncpy(games[idx].name, title_id, sizeof(games[idx].name) - 1);

    char sfo_path[128];
    snprintf(sfo_path, sizeof(sfo_path), "ur0:/appmeta/%s/param.sfo", title_id);
    if (!parse_sfo_title(sfo_path, games[idx].name, sizeof(games[idx].name))) {
        snprintf(sfo_path, sizeof(sfo_path), "ux0:/app/%s/sce_sys/param.sfo", title_id);
        parse_sfo_title(sfo_path, games[idx].name, sizeof(games[idx].name));
    }

    char icon_path[128];
    snprintf(icon_path, sizeof(icon_path), "ur0:/appmeta/%s/icon0.png", title_id);
    games[idx].icon = vita2d_load_PNG_file(icon_path);
    if (!games[idx].icon) {
        snprintf(icon_path, sizeof(icon_path), "ux0:/app/%s/sce_sys/icon0.png", title_id);
        games[idx].icon = vita2d_load_PNG_file(icon_path);
    }
    if (!games[idx].icon) {
        snprintf(icon_path, sizeof(icon_path), "ux0:/data/TrackPlug/Icons/%s.png", title_id);
        games[idx].icon = vita2d_load_PNG_file(icon_path);
    }
    return idx;
}

static void load_settings(void) {
    SceUID fd = sceIoOpen("ux0:/data/ActivityLog/settings.bin", SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoRead(fd, &dark_mode, sizeof(int));
        sceIoClose(fd);
    }
}

static void save_settings(void) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/ActivityLog", 0777);
    SceUID fd = sceIoOpen("ux0:/data/ActivityLog/settings.bin", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &dark_mode, sizeof(int));
        sceIoClose(fd);
    }
}

static void update_record_in_file(const char *path, const char *title_id, uint32_t secs) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/ActivityLog", 0777);
    SceUID fd = sceIoOpen(path, SCE_O_RDWR | SCE_O_CREAT, 0777);
    if (fd < 0) return;

    SnapshotRecord rec;
    SceOff offset = 0;
    int found = 0;
    while (sceIoRead(fd, &rec, sizeof(SnapshotRecord)) == sizeof(SnapshotRecord)) {
        if (strcmp(rec.title_id, title_id) == 0) {
            rec.seconds = secs;
            sceIoLseek(fd, offset, SCE_SEEK_SET);
            sceIoWrite(fd, &rec, sizeof(SnapshotRecord));
            found = 1;
            break;
        }
        offset += sizeof(SnapshotRecord);
    }
    if (!found) {
        memset(&rec, 0, sizeof(SnapshotRecord));
        strncpy(rec.title_id, title_id, 11);
        rec.seconds = secs;
        sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoWrite(fd, &rec, sizeof(SnapshotRecord));
    }
    sceIoClose(fd);
}

static int get_record_from_file(const char *path, const char *title_id, uint32_t *out_secs) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;

    SnapshotRecord rec;
    while (sceIoRead(fd, &rec, sizeof(SnapshotRecord)) == sizeof(SnapshotRecord)) {
        if (strcmp(rec.title_id, title_id) == 0) {
            *out_secs = rec.seconds;
            sceIoClose(fd);
            return 1;
        }
    }
    sceIoClose(fd);
    return 0;
}

static void append_daily_playtime(const char *title_id, uint32_t add_secs, uint32_t add_launches) {
    SceDateTime now;
    sceRtcGetCurrentClockLocalTime(&now);
    uint16_t y = sceRtcGetYear(&now);
    uint8_t m  = sceRtcGetMonth(&now);
    uint8_t d  = sceRtcGetDay(&now);

    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/ActivityLog", 0777);
    const char *path = "ux0:/data/ActivityLog/daily.bin";
    SceUID fd = sceIoOpen(path, SCE_O_RDWR | SCE_O_CREAT, 0777);
    if (fd < 0) return;

    DailyRecord rec;
    SceOff offset = 0;
    int found = 0;
    while (sceIoRead(fd, &rec, sizeof(DailyRecord)) == sizeof(DailyRecord)) {
        if (rec.year == y && rec.month == m && rec.day == d && strcmp(rec.title_id, title_id) == 0) {
            rec.seconds += add_secs;
            rec.launches += add_launches;
            sceIoLseek(fd, offset, SCE_SEEK_SET);
            sceIoWrite(fd, &rec, sizeof(DailyRecord));
            found = 1;
            break;
        }
        offset += sizeof(DailyRecord);
    }
    if (!found) {
        memset(&rec, 0, sizeof(DailyRecord));
        strncpy(rec.title_id, title_id, 11);
        rec.year = y;
        rec.month = m;
        rec.day = d;
        rec.seconds = add_secs;
        rec.launches = add_launches;
        sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoWrite(fd, &rec, sizeof(DailyRecord));
    }
    sceIoClose(fd);
}

static void load_daily_file(const char *path) {
    SceUID lfd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (lfd >= 0) {
        DailyRecord drec;
        while (daily_count < MAX_DAILY_RECORDS &&
               sceIoRead(lfd, &drec, sizeof(DailyRecord)) == sizeof(DailyRecord)) {
            daily_logs[daily_count++] = drec;
            find_or_add_game(drec.title_id);
        }
        sceIoClose(lfd);
    }
}

static void scan_all_data(void) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir("ux0:/data/ActivityLog", 0777);

    SceUID dfd = sceIoDopen("ux0:/app");
    if (dfd >= 0) {
        SceIoDirent dir;
        while (sceIoDread(dfd, &dir) > 0) {
            if (SCE_S_ISDIR(dir.d_stat.st_mode) && dir.d_name[0] != '.') {
                find_or_add_game(dir.d_name);
            }
        }
        sceIoDclose(dfd);
    }

    const char *snap_path = "ux0:/data/ActivityLog/tp_snapshot.bin";
    const char *base_path = "ux0:/data/ActivityLog/manual_base.bin";

    dfd = sceIoDopen("ux0:/data/TrackPlug/Records");
    if (dfd >= 0) {
        SceIoDirent dir;
        while (sceIoDread(dfd, &dir) > 0) {
            if (dir.d_name[0] == '.') continue;
            char tid[12];
            memset(tid, 0, sizeof(tid));
            for (int c = 0; c < 11 && dir.d_name[c] != '\0' && dir.d_name[c] != '.'; c++) {
                tid[c] = dir.d_name[c];
            }

            int idx = find_or_add_game(tid);
            if (idx >= 0) {
                char rpath[128];
                snprintf(rpath, sizeof(rpath), "ux0:/data/TrackPlug/Records/%s", dir.d_name);
                SceUID rfd = sceIoOpen(rpath, SCE_O_RDONLY, 0);
                if (rfd >= 0) {
                    uint32_t now_tp_secs = 0;
                    if (sceIoRead(rfd, &now_tp_secs, sizeof(uint32_t)) == sizeof(uint32_t)) {
                        uint32_t last_snap_secs = 0;
                        if (!get_record_from_file(snap_path, tid, &last_snap_secs)) {
                            update_record_in_file(snap_path, tid, now_tp_secs);
                            uint32_t existing_base = 0;
                            if (!get_record_from_file(base_path, tid, &existing_base)) {
                                update_record_in_file(base_path, tid, now_tp_secs);
                            }
                        } else if (now_tp_secs > last_snap_secs) {
                            uint32_t delta = now_tp_secs - last_snap_secs;
                            append_daily_playtime(tid, delta, 1);
                            update_record_in_file(snap_path, tid, now_tp_secs);
                        }
                    }
                    sceIoClose(rfd);
                }
            }
        }
        sceIoDclose(dfd);
    }

    SceUID bfd = sceIoOpen(base_path, SCE_O_RDONLY, 0);
    if (bfd >= 0) {
        SnapshotRecord rec;
        while (sceIoRead(bfd, &rec, sizeof(SnapshotRecord)) == sizeof(SnapshotRecord)) {
            int idx = find_or_add_game(rec.title_id);
            if (idx >= 0) games[idx].base_seconds = rec.seconds;
        }
        sceIoClose(bfd);
    }

    if (!daily_logs) daily_logs = calloc(MAX_DAILY_RECORDS, sizeof(DailyRecord));
    daily_count = 0;
    load_daily_file("ux0:/data/ActivityLog/daily.bin");
}

static int days_in_month(int y, int m) {
    static const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
    return dm[(m - 1) % 12];
}

static int to_linear_days(int y, int m, int d) {
    int days = y * 365 + (y / 4) - (y / 100) + (y / 400);
    for (int i = 1; i < m; i++) days += days_in_month(y, i);
    return days + d;
}

static void step_date(int delta_days) {
    sel_day += delta_days;
    while (sel_day > days_in_month(sel_year, sel_month)) {
        sel_day -= days_in_month(sel_year, sel_month);
        sel_month++;
        if (sel_month > 12) { sel_month = 1; sel_year++; }
    }
    while (sel_day < 1) {
        sel_month--;
        if (sel_month < 1) { sel_month = 12; sel_year--; }
        sel_day += days_in_month(sel_year, sel_month);
    }
}

static void recalculate_stats(TimeFilter filter, uint32_t *out_total_period) {
    *out_total_period = 0;
    int target_linear = to_linear_days(sel_year, sel_month, sel_day);

    for (int i = 0; i < game_count; i++) {
        games[i].logged_seconds = 0;
        games[i].period_seconds = (filter == FILTER_ALL_TIME) ? games[i].base_seconds : 0;
        games[i].total_launches = 0;
        games[i].first_year = 0;
        games[i].last_year = 0;
    }

    for (int r = 0; r < daily_count; r++) {
        DailyRecord *d = &daily_logs[r];
        int idx = find_or_add_game(d->title_id);
        if (idx < 0) continue;

        games[idx].logged_seconds += d->seconds;
        games[idx].total_launches += d->launches;

        if (games[idx].first_year == 0) {
            games[idx].first_year = d->year;
            games[idx].first_month = d->month;
            games[idx].first_day = d->day;
        }
        games[idx].last_year = d->year;
        games[idx].last_month = d->month;
        games[idx].last_day = d->day;

        int match = 0;
        if (filter == FILTER_DAY) {
            match = (d->year == sel_year && d->month == sel_month && d->day == sel_day);
        } else if (filter == FILTER_WEEK) {
            int rec_linear = to_linear_days(d->year, d->month, d->day);
            match = (rec_linear >= target_linear - 6 && rec_linear <= target_linear);
        } else if (filter == FILTER_MONTH) {
            match = (d->year == sel_year && d->month == sel_month);
        } else if (filter == FILTER_YEAR) {
            match = (d->year == sel_year);
        } else if (filter == FILTER_ALL_TIME) {
            match = 1;
        }

        if (match) {
            games[idx].period_seconds += d->seconds;
        }
    }

    for (int i = 0; i < game_count; i++) {
        *out_total_period += games[i].period_seconds;
    }
}

static void sort_games_by_period(void) {
    for (int i = 0; i < game_count - 1; i++) {
        for (int j = i + 1; j < game_count; j++) {
            if (games[j].period_seconds > games[i].period_seconds) {
                GameEntry tmp = games[i];
                games[i] = games[j];
                games[j] = tmp;
            }
        }
    }
}

int main(void) {
    SceAppUtilInitParam init_param;
    SceAppUtilBootParam boot_param;
    memset(&init_param, 0, sizeof(SceAppUtilInitParam));
    memset(&boot_param, 0, sizeof(SceAppUtilBootParam));
    sceAppUtilInit(&init_param, &boot_param);

    int enter_button = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter_button);
    if (enter_button == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE) {
        BTN_CONFIRM = SCE_CTRL_CIRCLE;
        BTN_CANCEL  = SCE_CTRL_CROSS;
        STR_CONFIRM = "O";
        STR_CANCEL  = "X";
    }

    SceDateTime now;
    sceRtcGetCurrentClockLocalTime(&now);
    sel_year  = sceRtcGetYear(&now);
    sel_month = sceRtcGetMonth(&now);
    sel_day   = sceRtcGetDay(&now);

    vita2d_init();
    vita2d_pgf *font = vita2d_load_default_pgf();

    load_settings();
    scan_all_data();

    AppState state = VIEW_DAILY_RECORDS;
    AppState prev_state = VIEW_DAILY_RECORDS;
    TimeFilter filter = FILTER_DAY;
    uint32_t total_period_secs = 0;
    int cursor = 0;
    int scroll = 0;
    int editing_time = 0;
    int edit_hours = 0, edit_mins = 0;

    recalculate_stats(filter, &total_period_secs);
    sort_games_by_period();

    SceCtrlData pad, old_pad;
    memset(&old_pad, 0, sizeof(SceCtrlData));

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~old_pad.buttons;

        if (pressed & SCE_CTRL_SQUARE) {
            dark_mode = !dark_mode;
            save_settings();
        }

        if (pressed & SCE_CTRL_START) {
            scan_all_data();
            recalculate_stats(filter, &total_period_secs);
            sort_games_by_period();
        }

        if (state == VIEW_GAME_DETAILS) {
            GameEntry *g = &games[cursor];
            if (!editing_time) {
                if (pressed & SCE_CTRL_TRIANGLE) {
                    editing_time = 1;
                    edit_hours = g->base_seconds / 3600;
                    edit_mins  = (g->base_seconds % 3600) / 60;
                } else if (pressed & BTN_CANCEL) {
                    state = prev_state;
                }
            } else {
                if (pressed & SCE_CTRL_UP)       edit_hours += 1;
                if (pressed & SCE_CTRL_DOWN)     edit_hours = (edit_hours > 0) ? edit_hours - 1 : 0;
                if (pressed & SCE_CTRL_RTRIGGER) edit_hours += 10;
                if (pressed & SCE_CTRL_LTRIGGER) edit_hours = (edit_hours >= 10) ? edit_hours - 10 : 0;
                if (pressed & SCE_CTRL_RIGHT)    edit_mins = (edit_mins + 5) % 60;
                if (pressed & SCE_CTRL_LEFT)     edit_mins = (edit_mins >= 5) ? edit_mins - 5 : 55;

                if (pressed & BTN_CONFIRM) {
                    g->base_seconds = (uint32_t)(edit_hours * 3600 + edit_mins * 60);
                    update_record_in_file("ux0:/data/ActivityLog/manual_base.bin", g->title_id, g->base_seconds);
                    recalculate_stats(filter, &total_period_secs);
                    editing_time = 0;
                } else if (pressed & BTN_CANCEL) {
                    editing_time = 0;
                }
            }
        } else {
            if (pressed & SCE_CTRL_SELECT) {
                state = (state == VIEW_DAILY_RECORDS) ? VIEW_LIBRARY_RANKINGS : VIEW_DAILY_RECORDS;
                if (state == VIEW_LIBRARY_RANKINGS && filter == FILTER_DAY) filter = FILTER_ALL_TIME;
                cursor = 0;
                scroll = 0;
                recalculate_stats(filter, &total_period_secs);
                sort_games_by_period();
            }
            if (pressed & SCE_CTRL_RTRIGGER) {
                filter = (filter + 1) % 5;
                cursor = 0; scroll = 0;
                recalculate_stats(filter, &total_period_secs);
                sort_games_by_period();
            }
            if (pressed & SCE_CTRL_LTRIGGER) {
                filter = (filter == 0) ? 4 : (filter - 1);
                cursor = 0; scroll = 0;
                recalculate_stats(filter, &total_period_secs);
                sort_games_by_period();
            }
            if (pressed & SCE_CTRL_LEFT) {
                int step = (filter == FILTER_WEEK) ? -7 : (filter == FILTER_MONTH) ? -30 : (filter == FILTER_YEAR) ? -365 : -1;
                step_date(step);
                recalculate_stats(filter, &total_period_secs);
                sort_games_by_period();
            }
            if (pressed & SCE_CTRL_RIGHT) {
                int step = (filter == FILTER_WEEK) ? 7 : (filter == FILTER_MONTH) ? 30 : (filter == FILTER_YEAR) ? 365 : 1;
                step_date(step);
                recalculate_stats(filter, &total_period_secs);
                sort_games_by_period();
            }
            if (pressed & SCE_CTRL_DOWN && game_count > 0) {
                cursor = (cursor + 1) % game_count;
                if (cursor >= scroll + 5) scroll = cursor - 4;
                if (cursor == 0) scroll = 0;
            }
            if (pressed & SCE_CTRL_UP && game_count > 0) {
                cursor = (cursor > 0) ? cursor - 1 : game_count - 1;
                if (cursor < scroll) scroll = cursor;
                if (cursor == game_count - 1 && game_count > 5) scroll = game_count - 5;
            }
            if ((pressed & BTN_CONFIRM) && game_count > 0) {
                prev_state = state;
                state = VIEW_GAME_DETAILS;
                editing_time = 0;
            }
        }

        uint32_t col_bg     = dark_mode ? RGBA8(28, 30, 34, 255)   : RGBA8(240, 242, 238, 255);
        uint32_t col_grid   = dark_mode ? RGBA8(42, 45, 52, 255)   : RGBA8(220, 224, 218, 255);
        uint32_t col_card   = dark_mode ? RGBA8(48, 52, 60, 255)   : RGBA8(255, 255, 255, 255);
        uint32_t col_select = dark_mode ? RGBA8(75, 110, 145, 255) : RGBA8(210, 238, 255, 255);
        uint32_t col_header = dark_mode ? RGBA8(40, 95, 145, 255)  : RGBA8(65, 155, 225, 255);
        uint32_t col_text   = dark_mode ? RGBA8(240, 240, 240, 255): RGBA8(45, 45, 45, 255);
        uint32_t col_bar    = dark_mode ? RGBA8(85, 190, 95, 255)  : RGBA8(105, 205, 90, 255);

        vita2d_start_drawing();
        vita2d_set_clear_color(col_bg);
        vita2d_clear_screen();

        for (int x = 0; x < 960; x += 24) vita2d_draw_line(x, 0, x, 544, col_grid);
        for (int y = 0; y < 544; y += 24) vita2d_draw_line(0, y, 960, y, col_grid);

        vita2d_draw_rectangle(0, 0, 960, 52, col_header);
        const char *filter_names[] = {"Day", "7-Day Week", "Month", "Year", "All-Time"};
        const char *mode_title = (state == VIEW_DAILY_RECORDS) ? "Daily Records" :
                                 (state == VIEW_LIBRARY_RANKINGS) ? "Software Library & Rankings" : "Software Details";
        vita2d_pgf_draw_text(font, 20, 34, RGBA8(255,255,255,255), 1.15f, mode_title);

        char date_str[128];
        if (filter == FILTER_DAY || filter == FILTER_WEEK)
            snprintf(date_str, sizeof(date_str), "< %04d/%02d/%02d >  [L/R] %s", sel_year, sel_month, sel_day, filter_names[filter]);
        else if (filter == FILTER_MONTH)
            snprintf(date_str, sizeof(date_str), "< %04d/%02d >  [L/R] %s", sel_year, sel_month, filter_names[filter]);
        else if (filter == FILTER_YEAR)
            snprintf(date_str, sizeof(date_str), "< %04d >  [L/R] %s", sel_year, filter_names[filter]);
        else
            snprintf(date_str, sizeof(date_str), "[L/R] Filter: All-Time Ranking");

        vita2d_pgf_draw_text(font, 520, 34, RGBA8(255,255,255,255), 1.0f, date_str);

        if (state == VIEW_GAME_DETAILS && game_count > 0) {
            GameEntry *g = &games[cursor];
            uint32_t total_sec = g->base_seconds + g->logged_seconds;
            uint32_t avg_sec   = (g->total_launches > 0) ? (total_sec / g->total_launches) : total_sec;

            vita2d_draw_rectangle(80, 80, 800, 380, col_card);
            vita2d_draw_rectangle(80, 80, 800, 6, col_header);

            if (g->icon) {
                vita2d_draw_texture_scale(g->icon, 110, 110, 96.0f / vita2d_texture_get_width(g->icon), 96.0f / vita2d_texture_get_height(g->icon));
            } else {
                vita2d_draw_rectangle(110, 110, 96, 96, col_header);
            }

            vita2d_pgf_draw_text(font, 230, 145, col_text, 1.3f, g->name);
            vita2d_pgf_draw_textf(font, 230, 180, col_text, 0.95f, "Title ID: %s", g->title_id);

            vita2d_pgf_draw_textf(font, 110, 250, col_text, 1.1f, "Total Time Played:     %02uh %02um %02us", total_sec / 3600, (total_sec % 3600) / 60, total_sec % 60);
            vita2d_pgf_draw_textf(font, 110, 290, col_text, 1.0f, "Baseline / Imported:   %02uh %02um", g->base_seconds / 3600, (g->base_seconds % 3600) / 60);
            vita2d_pgf_draw_textf(font, 110, 330, col_text, 1.0f, "Logged Daily Time:     %02uh %02um %02us   (Times Played: %u)", g->logged_seconds / 3600, (g->logged_seconds % 3600) / 60, g->logged_seconds % 60, g->total_launches);
            vita2d_pgf_draw_textf(font, 110, 370, col_text, 1.0f, "Average Play Time:     %02uh %02um", avg_sec / 3600, (avg_sec % 3600) / 60);

            if (g->first_year > 0) {
                vita2d_pgf_draw_textf(font, 110, 415, col_text, 0.95f, "First Logged: %04d/%02d/%02d     Last Logged: %04d/%02d/%02d",
                    g->first_year, g->first_month, g->first_day, g->last_year, g->last_month, g->last_day);
            }

            if (editing_time) {
                vita2d_draw_rectangle(220, 170, 520, 200, dark_mode ? RGBA8(25,28,35,250) : RGBA8(245,248,255,250));
                vita2d_draw_rectangle(220, 170, 520, 36, col_header);
                vita2d_pgf_draw_text(font, 240, 195, RGBA8(255,255,255,255), 1.0f, "Edit Baseline Playtime (Pre-Plugin Hours)");
                vita2d_pgf_draw_textf(font, 340, 265, col_text, 1.6f, "%03d h  :  %02d m", edit_hours, edit_mins);
                vita2d_pgf_draw_text(font, 245, 315, col_text, 0.85f, "[Up/Down] +/-1h   [L/R] +/-10h   [Left/Right] +/-5m");
                vita2d_pgf_draw_textf(font, 245, 345, col_text, 0.9f, "[%s] Save Changes     [%s] Cancel", STR_CONFIRM, STR_CANCEL);
            }
        } else {
            vita2d_draw_rectangle(30, 62, 900, 36, col_card);
            vita2d_pgf_draw_textf(font, 45, 86, col_text, 1.0f, "Period Total: %02uh %02um %02us   |   Games: %d   |   [START] Refresh",
                total_period_secs / 3600, (total_period_secs % 3600) / 60, total_period_secs % 60, game_count);

            uint32_t max_secs = (game_count > 0 && games[0].period_seconds > 0) ? games[0].period_seconds : 3600;

            for (int i = 0; i < 5 && (scroll + i) < game_count; i++) {
                int idx = scroll + i;
                GameEntry *g = &games[idx];
                int y = 110 + i * 74;

                vita2d_draw_rectangle(30, y, 900, 66, (idx == cursor) ? col_select : col_card);

                if (g->icon) {
                    vita2d_draw_texture_scale(g->icon, 42, y + 7, 52.0f / vita2d_texture_get_width(g->icon), 52.0f / vita2d_texture_get_height(g->icon));
                } else {
                    vita2d_draw_rectangle(42, y + 7, 52, 52, col_header);
                }

                vita2d_pgf_draw_textf(font, 108, y + 26, col_text, 1.0f, "#%d  %s", idx + 1, g->name);

                float ratio = (float)g->period_seconds / (float)max_secs;
                if (ratio > 1.0f) ratio = 1.0f;
                int bar_w = (int)(ratio * 500.0f);
                if (g->period_seconds > 0 && bar_w < 8) bar_w = 8;

                vita2d_draw_rectangle(108, y + 36, 500, 20, col_grid);
                if (bar_w > 0) vita2d_draw_rectangle(108, y + 36, bar_w, 20, col_bar);

                vita2d_pgf_draw_textf(font, 625, y + 51, col_text, 1.0f, "%02uh %02um %02us",
                    g->period_seconds / 3600, (g->period_seconds % 3600) / 60, g->period_seconds % 60);
            }
        }

        vita2d_draw_rectangle(0, 504, 960, 40, col_card);
        if (state == VIEW_GAME_DETAILS) {
            vita2d_pgf_draw_textf(font, 20, 530, col_text, 0.9f,
                "[Triangle] Edit Playtime   [%s] Back   [Square] %s Mode",
                STR_CANCEL, dark_mode ? "Light" : "Dark");
        } else {
            vita2d_pgf_draw_textf(font, 20, 530, col_text, 0.9f,
                "[%s] Info/Edit   [SELECT] View   [L/R] Filter   [START] Refresh   [Square] %s",
                STR_CONFIRM, dark_mode ? "Light" : "Dark");
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
        old_pad = pad;
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
