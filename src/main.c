/* main.c - reconstructed from LaunchMe 0x498-0x1154 (object boundary: docs/objects.md).
 * Startup, graphics setup, help pane, hidden images and the main pad loop.
 * File name, identifier names and parameter types are guesses; the code bytes are what is matched.
 */
#include "types.h"
#include "stdio.h"
#include "string.h"
#include "mem.h"
#include "kernel.h"
#include "nodes.h"
#include "kernelnodes.h"
#include "item.h"
#include "io.h"
#include "device.h"
#include "time.h"
#include "operror.h"
#include "graphics.h"
#include "filefunctions.h"
#include "displayutils.h"
#include "fontlib.h"
#include "textlib.h"
#include "event.h"
#include "controlpad.h"
#include "getvideoinfo.h"

/* kprintf is called through the swi.lib stub here (no __swi declaration from debug.h) */
extern void kprintf(const char *fmt, ...);

/* ode.c */
extern char  *ode_init(void);
extern uint32 ode_get_version(void);
extern uint32 ode_get_ext_id_b1(void);
extern int32  ode_c0_change_toc(uint32 toc_id);
/* ui.c */
extern void   browser_init(char *ode_name);
extern void   browser_input(uint32 buttons);
extern void   draw_side_pane(void);
extern void   draw_list_rows(void);
extern int32  is_about_mode(void);

void timer_delay(int32 secs, int32 usecs);

/* LaunchMe RW 0x93c8-0x9448 */
static Item     vram_ioreq = 0;         /* 0x93c8 */
ScreenContext  *sc = 0;                 /* 0x93cc */
static Item     display = 0;            /* 0x93d0 */
FontDescriptor *font = 0;               /* 0x93d4 */
static void    *bg_image = 0;           /* 0x93d8  assets/bg.IMAG */
static void    *st_image = 0;           /* 0x93dc  assets/st.IMAG (About) */
static uint32   seq1_pos = 0;           /* 0x93e0 */
static uint32   seq2_pos = 0;           /* 0x93e4 */
TextCel        *help_cel = 0;           /* 0x93e8 */
TextCel        *list_cels[14] = {0};    /* 0x93ec */
char            status_text[32] = {0};  /* 0x9424 */
static TextCel *status_cel = 0;         /* 0x9444 */

/* LaunchMe 0x1104 / 0x112c (const data: end of the code area) */
static const uint32 seq1[10] = {
    ControlUp, ControlUp, ControlDown, ControlDown, ControlLeft, ControlRight, ControlLeft, ControlRight,
    ControlB, ControlA
};
static const uint32 seq2[10] = {
    ControlDown, ControlDown, ControlUp, ControlUp, ControlRight, ControlLeft, ControlRight, ControlLeft,
    ControlA, ControlB
};

/* 0x498: Konami code */
int32 secret_seq1_step(uint32 buttons)
{
    if (seq1[seq1_pos] == buttons)
        seq1_pos++;
    else
        seq1_pos = 0;
    if (seq1_pos >= 10) {
        seq1_pos = 0;
        return 1;
    }
    return 0;
}

/* 0x51c: reverse Konami code */
int32 secret_seq2_step(uint32 buttons)
{
    if (seq2[seq2_pos] == buttons)
        seq2_pos++;
    else
        seq2_pos = 0;
    if (seq2_pos >= 10) {
        seq2_pos = 0;
        return 1;
    }
    return 0;
}

/* 0x59c */
int32 gfx_init(void)
{
    int32 retval = -1;
    int32 type;

    vram_ioreq = GetVRAMIOReq();
    if (vram_ioreq < 0)
        goto done;
    sc = (ScreenContext *)AllocMem(sizeof(ScreenContext), MEMTYPE_ANY);
    if (sc == 0)
        goto done;
    retval = OpenGraphicsFolio();
    if (retval < 0)
        goto done;
    type = GetDisplayType();
    if (type < 0)
        goto done;
    type = (type == DI_TYPE_PAL1 || type == DI_TYPE_PAL2) ? DI_TYPE_PAL1 : DI_TYPE_NTSC;
    display = CreateBasicDisplay(sc, type, 2);
    if (display < 0)
        goto done;
    retval = InitControlPad(1);
    if (retval < 0)
        goto done;
    font = LoadFont("assets/sans_serif_14.font", MEMTYPE_ANY);
    if (font == 0)
        retval = -1;
    else
        retval = 0;
    bg_image = LoadImage("assets/bg.IMAG", 0, 0, sc);
    st_image = LoadImage("assets/st.IMAG", 0, 0, sc);
done:
    if (retval < 0) {
        CloseGraphicsFolio();
        if (display > 0)
            DeleteBasicDisplay(sc);
        if (sc != 0)
            FreeMem(sc, sizeof(ScreenContext));
        if (vram_ioreq > 0)
            DeleteItem(vram_ioreq);
    }
    return retval;
}

/* 0x7cc */
void gfx_shutdown(void)
{
    KillControlPad();
    if (font != 0)
        UnloadFont(font);
    if (display > 0)
        DeleteBasicDisplay(sc);
    if (sc != 0)
        FreeMem(sc, sizeof(ScreenContext));
    if (vram_ioreq > 0)
        DeleteItem(vram_ioreq);
}

/* 0x86c: 14 list rows and the help text */
void create_help_pane(void)
{
    int32 i;

    for (i = 0; i < 14; i++) {
        list_cels[i] = CreateTextCel(font, 0, 210, font->fd_charHeight + 2);
        SetTextCelMargins(list_cels[i], 2, 2);
        SetTextCelColor(list_cels[i], 0, 0x6f7b);
        EraseTextInCel(list_cels[i]);
        SetTextCelCoords(list_cels[i], 0x5a0000, (i * (font->fd_charHeight + 2) + 8) << 16);
    }
    help_cel = CreateTextCel(font, 0, 130, font->fd_charHeight * 10 + 20);
    SetTextCelMargins(help_cel, 2, 2);
    SetTextCelColor(help_cel, 0, 0x6f7b);
    EraseTextInCel(help_cel);
    UpdateTextInCel(help_cel, 0, "A sel/add\n");
    UpdateTextInCel(help_cel, 0, "B back/clr\n");
    UpdateTextInCel(help_cel, 0, "C q.play\n");
    UpdateTextInCel(help_cel, 0, "L page ->\n");
    UpdateTextInCel(help_cel, 0, "R page <-\n");
    UpdateTextInCel(help_cel, 0, "P Play\n");
    UpdateTextInCel(help_cel, 0, "X Menu\n");
    SetTextCelCoords(help_cel, 0x60000, 0xa0000);
}

/* 0xa8c */
uint32 read_pad(void)
{
    uint32 buttons;
    int32 err;

    err = DoControlPad(1, &buttons, 0);
    return buttons;
}

/* 0xac8: 1 maus, 2 access.tuner, 3 walker (IMAG files) until a button is pressed */
void show_secret_image(int32 which)
{
    void *img;

    while (read_pad() != 0)
        ;
    switch (which) {
    case 3:
        img = LoadImage("$c/walker", 0, 0, sc);
        break;
    case 2:
        img = LoadImage("System/Drivers/TUNERS/access.tuner", 0, 0, sc);
        break;
    case 1:
        img = LoadImage("$c/maus", 0, 0, sc);
        break;
    default:
        return;
    }
    if (img == 0)
        printf("Babushka\n");
    do {
        sc->sc_CurrentScreen = 1 - sc->sc_CurrentScreen;
        DrawImage(sc->sc_ScreenItems[sc->sc_CurrentScreen], img, sc);
        DisplayScreen(sc->sc_ScreenItems[sc->sc_CurrentScreen], 0);
    } while (read_pad() == 0);
    UnloadImage(img);
}

/* 0xc48 */
void draw_frame(int32 with_list)
{
    sc->sc_CurrentScreen = 1 - sc->sc_CurrentScreen;
    if (is_about_mode()) {
        DrawImage(sc->sc_ScreenItems[sc->sc_CurrentScreen], st_image, sc);
    } else {
        DrawImage(sc->sc_ScreenItems[sc->sc_CurrentScreen], bg_image, sc);
        draw_side_pane();
        if (with_list)
            draw_list_rows();
    }
}

/* 0xd00 */
void display_frame(void)
{
    DisplayScreen(sc->sc_ScreenItems[sc->sc_CurrentScreen], 0);
}

/* 0xd3c */
int main(void)
{
    Item mplay;
    int32 unused;
    char *name;
    int32 ver;
    uint32 buttons;

    seq1_pos = 0;
    seq2_pos = 0;
    unused = 0;
    name = ode_init();
    if (name == 0) {
        kprintf("ode init failed\n");
        return 0;
    }
    if (gfx_init() < 0) {
        kprintf("Graph init failed\n");
        gfx_shutdown();
        return 0;
    }
    ode_c0_change_toc(0);
    create_help_pane();
    mplay = LoadProgram("$boot/mplay");
    status_cel = CreateTextCel(font, 0, 140, font->fd_charHeight + 2);
    SetTextCelMargins(status_cel, 2, 2);
    SetTextCelColor(status_cel, 0xf, 0x6f7b);
    EraseTextInCel(status_cel);
    UpdateTextInCel(status_cel, 1, "Waiting for opera init");
    SetTextCelCoords(status_cel, 0x820000, 0xa0000);
    ver = ode_get_version();
    ver = ver >> 16;
    ver -= 60;          /* the original subtracts 0x3c and 9 separately (eject count 69) */
    ver -= 9;
    if (ver == 0)
        show_secret_image(3);
    sc->sc_CurrentScreen = 1;
    DrawImage(sc->sc_ScreenItems[sc->sc_CurrentScreen], bg_image, sc);
    if (ode_get_ext_id_b1() & 0x20)
        DrawCels(sc->sc_BitmapItems[sc->sc_CurrentScreen], status_cel->tc_CCB);
    DisplayScreen(sc->sc_ScreenItems[sc->sc_CurrentScreen], 0);
    if (ode_get_ext_id_b1() & 0x20)
        timer_delay(2, 0);
    browser_init(name);
    for (;;) {
        draw_frame(1);
        display_frame();
        buttons = read_pad();
        if (buttons != 0) {
            if (secret_seq1_step(buttons))
                show_secret_image(1);
            if (secret_seq2_step(buttons))
                show_secret_image(2);
            browser_input(buttons);
            while (read_pad() != 0)
                ;
        }
    }
    gfx_shutdown();     /* unreachable, present in the original */
    return 0;
}

/* 0xff8: wait on the timer device (TIMERCMD_DELAY, TIMER_UNIT_USEC) */
void timer_delay(int32 secs, int32 usecs)
{
    IOInfo ioi;
    TimeVal tv;
    Item dev;
    Item req;
    Err err;

    tv.tv_sec = secs;
    tv.tv_usec = usecs;
    dev = -1;
    req = -1;
    dev = FindAndOpenDevice("timer");
    if (dev < 0)
        goto done;
    req = CreateIOReq(0, 0, dev, 0);
    if (req < 0)
        goto done;
    memset(&ioi, 0, sizeof(ioi));
    ioi.ioi_Command = TIMERCMD_DELAY;
    ioi.ioi_Unit = TIMER_UNIT_USEC;
    ioi.ioi_Send.iob_Buffer = &tv;
    ioi.ioi_Send.iob_Len = sizeof(tv);
    err = DoIO(req, &ioi);
    if (err < 0)
        PrintfSysErr(err);
done:
    if (req >= 0)
        DeleteItem(req);
    else
        PrintfSysErr(req);
    if (dev >= 0)
        CloseItem(dev);
    else
        PrintfSysErr(dev);
}
