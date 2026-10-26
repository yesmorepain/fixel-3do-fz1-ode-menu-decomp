/* ui.c - reconstructed from LaunchMe 0x2c4c-0x4db4 (object boundary: docs/objects.md).
 * File browser (D1 TOC pages, play queue), X menu (NVRAM save/load, ROM/RAM dumps, update, device info, about),
 * side pane and status line.
 * File name, identifier names and parameter types are guesses; the code bytes are what is matched.
 */
#include "types.h"
#include "stdio.h"
#include "string.h"
#include "graphics.h"
#include "filefunctions.h"
#include "displayutils.h"
#include "fontlib.h"
#include "textlib.h"
#include "event.h"

#define MAX_ROWS    14
#define BIGBUF_SIZE 0x8000

typedef struct TocEntry {       /* 0x2c bytes, from a D1 record */
    uint32 flags;               /* bit 0: game (playable), bit 1: directory shown as [name] */
    uint32 toc_id;
    uint32 name_len;
    char   name[32];
} TocEntry;

typedef void (*MenuHandler)(uint32 buttons);

typedef struct MenuItem {
    char        *label;
    MenuHandler  handler;
} MenuItem;

/* main.c */
extern ScreenContext  *sc;
extern FontDescriptor *font;
extern TextCel        *help_cel;
extern TextCel        *list_cels[14];
extern char            status_text[32];
extern uint32 read_pad(void);
extern void   draw_frame(int32 with_list);
extern void   display_frame(void);
extern void   timer_delay(int32 secs, int32 usecs);
/* ode.c */
extern uint32 ode_get_version(void);
extern void   ode_refresh_ext_id(void);
extern int32  ode_cmd(uint8 *cmd, uint8 *reply, int32 replylen);
extern int32  ode_c0_change_toc(uint32 toc_id);
extern int32  ode_c3_clear_playlist(void);
extern int32  ode_c4_add_playlist(uint32 toc_id);
extern int32  ode_c5_launch_playlist(void);
extern int32  ode_fill_buffer(uint8 *buf, uint32 len);
extern int32  ode_e1_open_file(char *name, uint32 toc_id, uint32 mode);
extern int32  ode_e7_read_buffer(uint8 *dst, uint32 len);
extern int32  ode_e3_read_file(uint32 len, uint32 *n);
extern int32  ode_e4_write_file(uint32 len, uint32 *n);
extern int32  ode_e5_close_file(void);
extern int32  ode_f0_update_ode(void);
extern int32  ode_d1_get_toc_list(uint32 toc_id, uint32 index, uint8 *out, int32 len);
extern int32  ramp_read(int32 unit, uint32 addr, void *buf, int32 len);
extern int32  ramp_write(int32 unit, uint32 addr, void *buf, int32 len);
extern Item   nvram_unmount(void);
extern Item   run_fscheck(void);
/* crc.c, rampatch.c */
extern uint32 crc32_update(uint32 crc, const uint8 *buf, int32 len);
extern uint32 ram_crc_0x200(void);
extern int32  apply_ram_patches(int32 arg);

void xmenu_storage_manager(uint32 buttons);
void xmenu_save_nvram(uint32 buttons);
void xmenu_load_nvram(uint32 buttons);
void xmenu_update_ode(uint32 buttons);
void xmenu_dump_ram(uint32 buttons);
void xmenu_dump_sysrom(uint32 buttons);
void xmenu_dump_fontrom(uint32 buttons);
void xmenu_dump_fmvrom(uint32 buttons);
void xmenu_about(uint32 buttons);
void xmenu_device_info(uint32 buttons);
void show_status(char *msg);
int32 browser_load_page(int32 start);
void list_highlight(void);
int32 xmenu_draw(void);
int32 xmenu_input(uint32 buttons);

/* LaunchMe RW 0x94d4-0x9728 */
static uint32 queued = 0;                   /* 0x94d4  bit per row: queued for the playlist */
static int32  row_count[4] = {0};           /* 0x94d8  rows shown, per mode */
static int32  cursor[4] = {0};              /* 0x94e8  selected row, per mode */
static int32  page_start[4] = {0};          /* 0x94f8  D1 index of the first row (only [0] used) */
static int32  mode = 0;                     /* 0x9508  0 browser, 1 X menu, 2 about, 3 device info */
static int32  queue_count = 0;              /* 0x950c */
static TextCel *progress_cel = 0;           /* 0x9510  status line */
static int32  patches_applied = 0;          /* 0x9514 */
static MenuItem xmenu_items[] = {           /* 0x9518 */
    { "Storage Manager", xmenu_storage_manager },
    { "Save NVRam to current dir", xmenu_save_nvram },
    { "Load NVRam from current dir", xmenu_load_nvram },
    { "Dump SYSRom", xmenu_dump_sysrom },
    { "Dump FontRom", xmenu_dump_fontrom },
    { "Dump FMVRom", xmenu_dump_fmvrom },
    { "Dump RAM 3mb", xmenu_dump_ram },
    { "Update ODE", xmenu_update_ode },
    { "Device info", xmenu_device_info },
    { "About", xmenu_about },
    { 0, 0 }
};
static MenuItem about_items[] = {           /* 0x9570  label 0 = ODE name, set by browser_init */
    { 0, 0 },
    { "", 0 },
    { "(c) 2022 Felix Lazarev", 0 },
    { "Special thanks to:", 0 },
    { "1.", 0 },
    { "2.", 0 },
    { "3.", 0 },
    { "4.", 0 },
    { "many more ...", 0 },
    { 0, 0 }
};
static char devinfo_madam[32] = {0};   /* 0x95c0 */
static char devinfo_clio[32] = {0};   /* 0x95e0 */
static char devinfo_dipir[32] = {0};   /* 0x9600 */
static char devinfo_sysrom[32] = {0};   /* 0x9620 */
static char devinfo_fontrom[32] = {0};   /* 0x9640 */
static char devinfo_fmvrom[32] = {0};   /* 0x9660 */
static char devinfo_patches[32] = {0};   /* 0x9680 */
static MenuItem devinfo_items[] = {         /* 0x96a0 */
    { devinfo_madam, 0 },
    { devinfo_clio, 0 },
    { devinfo_dipir, 0 },
    { devinfo_sysrom, 0 },
    { devinfo_fontrom, 0 },
    { devinfo_fmvrom, 0 },
    { devinfo_patches, 0 },
    { 0, 0 }
};
static MenuItem *menus[4] = { 0, xmenu_items, about_items, devinfo_items };     /* 0x96e0 */
static TocEntry entry_tmp = {0};            /* 0x96f0 */
static uint32 sysrom_crc = 0;               /* 0x971c */
static uint32 fontrom_crc = 0;              /* 0x9720 */
static uint32 fmvrom_crc = 0;               /* 0x9724 */

/* LaunchMe ZI */
static TocEntry entries[MAX_ROWS];          /* 0x9f8c */
static uint8 d1_buf[0x800];                 /* 0xa1f4 */
static uint8 big_buf[BIGBUF_SIZE];          /* 0xa9f4 */

/* LaunchMe 0x4da4 (const data: end of the code area): selected-row background per mode */
static const uint32 highlight[4] = { 0xf, 0x1e0, 0, 0 };

/* 0x2d3c */
void side_pane_update(char *extra)
{
    char buf[16];
    uint32 ver;

    ver = ode_get_version();
    EraseTextInCel(help_cel);
    UpdateTextInCel(help_cel, 0, "A sel/add\n");
    UpdateTextInCel(help_cel, 0, "B clear q\n");
    UpdateTextInCel(help_cel, 0, "C faststart\n");
    UpdateTextInCel(help_cel, 0, "L page ->\n");
    UpdateTextInCel(help_cel, 0, "R page <-\n");
    UpdateTextInCel(help_cel, 0, "P Play q\n");
    UpdateTextInCel(help_cel, 0, "X Menu\n");
    sprintf(buf, "%s:%s\n", about_items[0].label, "r5");
    UpdateTextInCel(help_cel, 0, buf);
    sprintf(buf, "v %d.%d.%d\n", (ver >> 8) & 0xff, ver & 0xff, (ver >> 16) & 0xff);
    UpdateTextInCel(help_cel, 0, buf);
    if (extra != 0)
        UpdateTextInCel(help_cel, 0, extra);
}

/* 0x2ef4 */
void browser_init(char *name)
{
    about_items[0].label = name;
    queue_count = 0;
    side_pane_update(0);
    if (name[0] == 'X')
        patches_applied = apply_ram_patches(1);
    else
        patches_applied = 0;
    mode = 0;
    queued = 0;
    cursor[0] = 0;
    row_count[0] = 0;
    page_start[0] = 0;
    row_count[0] = browser_load_page(page_start[0]);
    list_highlight();
    progress_cel = CreateTextCel(font, 2, 210, font->fd_charHeight + 2);
    SetTextCelMargins(progress_cel, 2, 2);
    SetTextCelColor(progress_cel, 0xf, 0x6f7b);
    EraseTextInCel(progress_cel);
    UpdateTextInCel(progress_cel, 1, "Progress");
    SetTextCelCoords(progress_cel, 0x5a0000, 0x6e0000);
}

/* 0x3058 */
void browser_input(uint32 buttons)
{
    if (buttons & ControlUp)
        cursor[mode]--;
    if (buttons & ControlDown)
        cursor[mode]++;
    if (buttons & ControlRightShift) {
        queued = 0;
        cursor[mode] = row_count[mode];
    }
    if (buttons & ControlLeftShift) {
        queued = 0;
        cursor[mode] = -1;
    }
    if (cursor[mode] < 0) {
        cursor[mode] = 0;
        switch (mode) {
        case 0:
            if (page_start[0] != 0) {
                page_start[0] -= row_count[0];
                if (page_start[0] <= 0)
                    page_start[0] = 0;
                row_count[0] = browser_load_page(page_start[0]);
                cursor[0] = row_count[0] - 1;
            }
            break;
        }
    }
    if (cursor[mode] >= row_count[mode]) {
        switch (mode) {
        case 0:
            if (cursor[0] >= MAX_ROWS) {
                row_count[0] = browser_load_page(page_start[0] + row_count[0]);
                if (row_count[0] == 0) {
                    row_count[0] = browser_load_page(page_start[0]);
                    cursor[0] = row_count[0] - 1;
                } else {
                    cursor[0] = 0;
                    page_start[0] += row_count[0];
                }
            } else {
                cursor[0] = row_count[0] - 1;
            }
            break;
        default:
            cursor[mode] = row_count[mode] - 1;
            break;
        }
    }
    if (buttons & (ControlA | ControlB)) {
        switch (mode) {
        case 0:
            if (entries[cursor[0]].flags & 1) {
                if (buttons & ControlA) {
                    if (ode_c4_add_playlist(entries[cursor[0]].toc_id) >= 0) {
                        queued |= 1 << cursor[0];
                        queue_count++;
                        sprintf(status_text, "Queue %d", queue_count);
                        side_pane_update(status_text);
                    }
                }
                if (buttons & ControlB) {
                    queued = 0;
                    queue_count = 0;
                    ode_c3_clear_playlist();
                    side_pane_update(0);
                }
            } else {
                if (buttons & ControlA) {
                    ode_c0_change_toc(entries[cursor[0]].toc_id);
                    row_count[0] = browser_load_page(0);
                    cursor[0] = 0;
                    queued = 0;
                }
            }
            break;
        default:
            xmenu_input(buttons);
            break;
        }
    }
    if (buttons & ControlC) {
        switch (mode) {
        case 0:
            if (entries[cursor[0]].flags & 1) {
                ode_c3_clear_playlist();
                ode_c4_add_playlist(entries[cursor[0]].toc_id);
                ode_c5_launch_playlist();
                show_status("Starting the game");
                timer_delay(7, 0);
            }
            break;
        default:
            xmenu_input(buttons);
            break;
        }
    }
    if (buttons & ControlStart) {
        switch (mode) {
        case 0:
            if (queue_count != 0) {
                ode_c5_launch_playlist();
                show_status("Starting the queue");
                timer_delay(7, 0);
            }
            break;
        default:
            xmenu_input(buttons);
            break;
        }
    }
    if (buttons & ControlX) {
        queued = 0;
        switch (mode) {
        case 0:
            mode = 1;
            row_count[1] = xmenu_draw();
            break;
        case 1:
        case 2:
            mode = 0;
            row_count[0] = browser_load_page(page_start[0]);
            break;
        }
    }
    list_highlight();
}

/* 0x35dc */
void draw_list_rows(void)
{
    int32 i;

    if (row_count[mode] > MAX_ROWS)
        row_count[mode] = MAX_ROWS;
    for (i = 0; i < row_count[mode]; i++)
        DrawCels(sc->sc_BitmapItems[sc->sc_CurrentScreen], list_cels[i]->tc_CCB);
}

/* 0x3678 */
void draw_side_pane(void)
{
    DrawCels(sc->sc_BitmapItems[sc->sc_CurrentScreen], help_cel->tc_CCB);
}

/* 0x36b4 */
void list_highlight(void)
{
    int32 i;
    int32 bg;
    uint32 sel;
    int32 fg;

    sel = 1 << cursor[mode];
    if (row_count[mode] > MAX_ROWS)
        row_count[mode] = MAX_ROWS;
    for (i = 0; row_count[mode] > i; i++) {
        bg = (sel & (1 << i)) ? highlight[mode] : 0;
        fg = (queued & (1 << i)) ? 0x7bc0 : 0x6f7b;
        SetTextCelColor(list_cels[i], bg, fg);
    }
}

/* 0x37a8: D1 record -> TocEntry; returns the record size (4 for the end marker) */
int32 parse_toc_entry(uint8 *p, TocEntry *e)
{
    int32 pos = 0;
    uint32 len;

    if (p == 0 || e == 0)
        return pos;
    e->flags = p[pos++] << 24;
    e->flags |= p[pos++] << 16;
    e->flags |= p[pos++] << 8;
    e->flags |= p[pos++];
    if (e->flags == 0xffffffff)
        return pos;
    e->toc_id = p[pos++] << 24;
    e->toc_id |= p[pos++] << 16;
    e->toc_id |= p[pos++] << 8;
    e->toc_id |= p[pos++];
    e->name_len = p[pos++] << 24;
    e->name_len |= p[pos++] << 16;
    e->name_len |= p[pos++] << 8;
    e->name_len |= p[pos++];
    len = e->name_len;
    if (len > 32)
        len = 32;
    memcpy(e->name, p + pos, len);
    e->name[31] = 0;
    return pos + e->name_len;
}

/* 0x394c: fill up to 14 rows from D1 index start, in D1 order */
int32 browser_load_page(int32 start)
{
    int32 idx = start;
    uint32 n = 0;
    uint32 off;
    int32 len;
    int32 err;

    do {
        if (n >= MAX_ROWS)
            return n;
        err = ode_d1_get_toc_list(idx, MAX_ROWS - n, d1_buf, 0x800);
        if (err < 0)
            return n;
        off = 0;
        do {
            len = parse_toc_entry(d1_buf + off, &entry_tmp);
            if (entry_tmp.flags == 0xffffffff)
                return n;
            if (entry_tmp.flags != 0xffffffff) {
                memcpy(&entries[n], &entry_tmp, sizeof(TocEntry));
                EraseTextInCel(list_cels[n]);
                entries[n].name[30] = 0;
                if (entries[n].flags & 2)
                    sprintf(status_text, "[%s]", entries[n].name);
                else
                    sprintf(status_text, "%s", entries[n].name);
                UpdateTextInCel(list_cels[n], 1, status_text);
            }
            idx++;
            n++;
            off += len;
        } while (off < 0x7f4);
    } while (n < MAX_ROWS);
    return n;
}

/* 0x3ad8 */
int32 xmenu_draw(void)
{
    uint32 i;

    if (menus[mode] == 0)
        return 0;
    for (i = 0; i < MAX_ROWS; i++)
        EraseTextInCel(list_cels[i]);
    i = 0;
    do {
        UpdateTextInCel(list_cels[i], 1, menus[mode][i].label);
        i++;
    } while (menus[mode][i].label != 0);
    return i;
}

/* 0x3b94 */
int32 xmenu_input(uint32 buttons)
{
    if (menus[mode] == 0)
        return 0;
    if (menus[mode][cursor[mode]].handler != 0) {
        menus[mode][cursor[mode]].handler(buttons);
        return 1;
    }
    if (mode == 2 || mode == 3) {
        mode = 1;
        row_count[mode] = xmenu_draw();
    }
    return 0;
}

/* 0x3c80 */
void xmenu_storage_manager(uint32 buttons)
{
    int32 unused1;
    int32 unused2;

    show_status("Loading ...");
    LoadProgram("assets/storagetuner");
    do {
        buttons = read_pad();
    } while (buttons != ControlX);
}

/* 0x3cdc */
void xmenu_save_nvram(uint32 buttons)
{
    uint32 n;
    char buf[32];
    int32 err;
    uint32 off;
    uint32 cs = 0;

    err = ramp_read(3, 0, big_buf, BIGBUF_SIZE);
    if (err < 0) {
        printf("Error reading sysrom %x\n", err);
        return;
    }
    err = ode_e1_open_file("nvram.nvm", 0, 1);
    if (err < 0) {
        printf("Error creating file\n");
        return;
    }
    for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
        sprintf(buf, "Saving NVRAM %d%%", off * 100 / BIGBUF_SIZE);
        show_status(buf);
        err = ode_fill_buffer(big_buf + off, 0x800);
        if (err < 0) {
            printf("Error sending data\n");
            break;
        }
        err = ode_e4_write_file(0x800, &n);
        if (err < 0)
            printf("error writing data\n");
    }
    for (off = 0; off < BIGBUF_SIZE; off++)
        cs += big_buf[off];
    printf("saving cs %x\n", cs);
    err = ode_e5_close_file();
    if (err < 0)
        printf("Error closing file\n");
}

/* 0x3ed4 */
void xmenu_load_nvram(uint32 buttons)
{
    uint32 n;
    char buf[32];
    int32 err;
    uint32 off;
    uint32 cs;
    uint8 *data = big_buf;     /* unused; loaded into a register by the original */

    show_status("Unmounting nvram");
    nvram_unmount();
    timer_delay(0, 5000);
    cs = 0;
    err = ode_e1_open_file("nvram.nvm", 0, 0);
    if (err < 0) {
        printf("no file\n");
        show_status("No nvram file");
    } else {
        for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
            printf("offset %x\n", off);
            sprintf(buf, "Loading NVRAM %d%%", off * 100 / BIGBUF_SIZE);
            show_status(buf);
            err = ode_e3_read_file(0x800, &n);
            if (err < 0) {
                show_status("Error loading file");
                break;
            }
            if (n > 0x800 || n == 0) {
                sprintf(status_text, "Wrong chunk sz %d", n);
                show_status(status_text);
                break;
            }
            printf("actual %x\n");
            err = ode_e7_read_buffer(big_buf + off, n);
            if (err < 0) {
                printf("error recv %x\n", err);
                show_status("Error recv chunk");
                break;
            }
            printf("recv %d\n", err);
        }
        for (off = 0; off < BIGBUF_SIZE; off++)
            cs += big_buf[off];
        printf("mycs %x\n", cs);
        err = ode_e5_close_file();
        printf("close %x\n", err);
        err = ramp_write(3, 0, big_buf, BIGBUF_SIZE);
        if (err < 0)
            printf("Error writing nvram %x\n", err);
        else
            show_status("Mounting nvram");
    }
    run_fscheck();
    timer_delay(0, 5000);
}

/* 0x41c0 */
void xmenu_update_ode(uint32 buttons)
{
    int32 err;

    err = ode_f0_update_ode();
    if (err == 0) {
        show_status("Updating. Don't turn off power!");
        timer_delay(15, 0);
        ode_refresh_ext_id();
        if (queue_count != 0) {
            sprintf(status_text, "Queue %d", queue_count);
            side_pane_update(status_text);
        } else {
            side_pane_update(0);
        }
    } else {
        show_status("Update not found");
        timer_delay(1, 0);
    }
}

/* 0x4298: memory 0..0x2fffff (ramp unit 0) -> ram3b.nvm */
void xmenu_dump_ram(uint32 buttons)
{
    char buf[32];
    uint32 n;
    int32 err;
    int32 addr;
    uint32 off;
    uint8 *data = big_buf;     /* unused; loaded into a register by the original */

    err = ode_e1_open_file("ram3b.nvm", 0, 1);
    if (err < 0) {
        printf("Error creating file\n");
        return;
    }
    for (addr = 0; addr < 0x300000; addr += BIGBUF_SIZE) {
        err = ramp_read(0, addr, big_buf, BIGBUF_SIZE);
        if (err < 0) {
            printf("Error reading sysrom %x\n", err);
            return;
        }
        for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
            sprintf(buf, "Saving RAM %x", addr + off);
            show_status(buf);
            err = ode_fill_buffer(big_buf + off, 0x800);
            if (err < 0) {
                printf("Error sending data\n");
                break;
            }
            err = ode_e4_write_file(0x800, &n);
            if (err < 0) {
                printf("error writing data\n");
                break;
            }
        }
    }
    err = ode_e5_close_file();
    if (err < 0)
        printf("Error closing file\n");
    printf("sysr : ");
    for (addr = 0; addr < 16; addr++)
        printf("%2.2x ", big_buf[addr]);
    printf("\n");
}

/* 0x4448: SYSROM 1 MB (ramp unit 2) -> sysrom.nvm */
void xmenu_dump_sysrom(uint32 buttons)
{
    char buf[32];
    uint32 n;
    int32 err;
    int32 addr;
    uint32 off;
    uint8 *data = big_buf;     /* unused; loaded into a register by the original */

    err = ode_e1_open_file("sysrom.nvm", 0, 1);
    if (err < 0) {
        printf("Error creating file\n");
        return;
    }
    for (addr = 0; addr < 0x100000; addr += BIGBUF_SIZE) {
        err = ramp_read(2, addr, big_buf, BIGBUF_SIZE);
        if (err < 0) {
            printf("Error reading sysrom %x\n", err);
            return;
        }
        for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
            sprintf(buf, "Saving SYSRom %x", addr + off);
            show_status(buf);
            err = ode_fill_buffer(big_buf + off, 0x800);
            if (err < 0) {
                printf("Error sending data\n");
                break;
            }
            err = ode_e4_write_file(0x800, &n);
            if (err < 0) {
                printf("error writing data\n");
                break;
            }
        }
    }
    err = ode_e5_close_file();
    if (err < 0)
        printf("Error closing file\n");
    printf("sysr : ");
    for (addr = 0; addr < 16; addr++)
        printf("%2.2x ", big_buf[addr]);
    printf("\n");
}

/* 0x45dc: font ROM 1 MB (ramp unit 5) -> fontrom.nvm */
void xmenu_dump_fontrom(uint32 buttons)
{
    char buf[32];
    uint32 n;
    int32 err;
    int32 addr;
    uint32 off;
    uint8 *data = big_buf;     /* unused; loaded into a register by the original */

    err = ode_e1_open_file("fontrom.nvm", 0, 1);
    if (err < 0) {
        printf("Error creating file\n");
        return;
    }
    for (addr = 0; addr < 0x100000; addr += BIGBUF_SIZE) {
        err = ramp_read(5, addr, big_buf, BIGBUF_SIZE);
        if (err < 0) {
            printf("Error reading sysrom %x\n", err);
            return;
        }
        for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
            sprintf(buf, "Saving FONTRom %x", addr + off);
            show_status(buf);
            err = ode_fill_buffer(big_buf + off, 0x800);
            if (err < 0) {
                printf("Error sending data\n");
                break;
            }
            err = ode_e4_write_file(0x800, &n);
            if (err < 0) {
                printf("error writing data\n");
                break;
            }
        }
    }
    err = ode_e5_close_file();
    if (err < 0)
        printf("Error closing file\n");
    printf("font : ");
    for (addr = 0; addr < 16; addr++)
        printf("%2.2x ", big_buf[addr]);
    printf("\n");
}

/* 0x4774: FMV ROM 1 MB (ramp unit 4) -> fmvrom.nvm */
void xmenu_dump_fmvrom(uint32 buttons)
{
    char buf[32];
    uint32 n;
    int32 err;
    int32 addr;
    uint32 off;
    uint8 *data = big_buf;     /* unused; loaded into a register by the original */

    err = ode_e1_open_file("fmvrom.nvm", 0, 1);
    if (err < 0) {
        printf("Error creating file\n");
        return;
    }
    for (addr = 0; addr < 0x100000; addr += BIGBUF_SIZE) {
        err = ramp_read(4, addr, big_buf, BIGBUF_SIZE);
        if (err < 0) {
            printf("Error reading sysrom %x\n", err);
            return;
        }
        for (off = 0; off < BIGBUF_SIZE; off += 0x800) {
            sprintf(buf, "Saving FMVrom %x", addr + off);
            show_status(buf);
            err = ode_fill_buffer(big_buf + off, 0x800);
            if (err < 0) {
                printf("Error sending data\n");
                break;
            }
            err = ode_e4_write_file(0x800, &n);
            if (err < 0) {
                printf("error writing data\n");
                break;
            }
        }
    }
    err = ode_e5_close_file();
    if (err < 0)
        printf("Error closing file\n");
    printf("fmv : ");
    for (addr = 0; addr < 16; addr++)
        printf("%2.2x ", big_buf[addr]);
    printf("\n");
}

/* 0x4914 */
void xmenu_about(uint32 buttons)
{
    mode = 2;
    row_count[mode] = xmenu_draw();
}

/* 0x4954 */
void xmenu_device_info(uint32 buttons)
{
    uint32 v;
    uint8 hdr[16];
    uint32 off;
    int32 err;

    mode = 3;
    show_status("Calculating ...");
    sprintf(devinfo_patches, "PATCHES: %d", patches_applied);
    ramp_read(0, 0x03300000, &v, 4);
    sprintf(devinfo_madam, "MADAM  : %.8X", v);
    ramp_read(0, 0x03400000, &v, 4);
    sprintf(devinfo_clio, "CLIO   : %.8X", v);
    sprintf(devinfo_dipir, "DIPIR  : %.8X", ram_crc_0x200());
    if (sysrom_crc == 0) {
        v = 0xffffffff;
        for (off = 0; off < 0x100000; off += BIGBUF_SIZE) {
            ramp_read(2, off, big_buf, BIGBUF_SIZE);
            v = crc32_update(v, big_buf, BIGBUF_SIZE);
        }
        sysrom_crc = ~v;
    }
    sprintf(devinfo_sysrom, "SYSRom : %.8X", sysrom_crc);
    if (fontrom_crc == 0) {
        ramp_read(2, 0, big_buf, 16);
        ramp_read(5, 0, hdr, 16);
        if (memcmp(big_buf, hdr, 16) != 0) {
            v = 0xffffffff;
            for (off = 0; off < 0x100000; off += BIGBUF_SIZE) {
                ramp_read(5, off, big_buf, BIGBUF_SIZE);
                v = crc32_update(v, big_buf, BIGBUF_SIZE);
            }
            fontrom_crc = ~v;
        }
    }
    if (fontrom_crc != 0)
        sprintf(devinfo_fontrom, "FONTRom: %.8X", fontrom_crc);
    else
        sprintf(devinfo_fontrom, "FONTRom: not present", fontrom_crc);
    if (fmvrom_crc == 0) {
        err = ramp_read(4, 0, hdr, 16);
        if (err == 0) {
            v = 0xffffffff;
            for (off = 0; off < 0x100000; off += BIGBUF_SIZE) {
                ramp_read(4, off, big_buf, BIGBUF_SIZE);
                v = crc32_update(v, big_buf, BIGBUF_SIZE);
            }
            fmvrom_crc = ~v;
        }
    }
    if (fmvrom_crc != 0)
        sprintf(devinfo_fmvrom, "FMVRom : %.8X", fmvrom_crc);
    else
        sprintf(devinfo_fmvrom, "FMVRom : not present", fmvrom_crc);
    row_count[mode] = xmenu_draw();
}

/* 0x4d08 */
void show_status(char *msg)
{
    UpdateTextInCel(progress_cel, 1, msg);
    draw_frame(0);
    DrawCels(sc->sc_BitmapItems[sc->sc_CurrentScreen], progress_cel->tc_CCB);
    display_frame();
}

/* 0x4d74 */
int32 is_about_mode(void)
{
    return mode == 2;
}
