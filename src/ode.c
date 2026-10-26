/* ode.c - reconstructed from LaunchMe 0x18d0-0x2c4c (object boundary: docs/objects.md).
 * The ODE command layer: every ODE command is built here and sent through the "ode" device (System/Drivers/ode),
 * plus the "ramp" device (memory / NVRAM access) and two LoadProgram helpers.
 * File name, identifier names and parameter types are guesses; the code bytes are what is matched.
 */
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "debug.h"
#include "nodes.h"
#include "kernelnodes.h"
#include "item.h"
#include "io.h"
#include "device.h"
#include "operror.h"
#include "filefunctions.h"

#define ODE_XFER_SIZE   0x800

/* data-phase descriptor handed to the ode driver by ode-device command 12 (via ioi_User) */
typedef struct OdeRW {
    uint32  unit;
    uint8  *buf;
    int32   len;
} OdeRW;

/* LaunchMe RW 0x94a0-0x94d4 */
static Item   ode_dev = 0;          /* 0x94a0 */
static Item   ramp_dev = 0;         /* 0x94a4 */
static char   ode_name[16] = {0};   /* 0x94a8  printable part of the EXT_ID reply */
static uint32 ext_id_b1 = 0;        /* 0x94b8  EXT_ID reply[1] */
static uint32 ver_major = 0;        /* 0x94bc  EXT_ID reply[8] */
static uint32 ver_minor = 0;        /* 0x94c0  EXT_ID reply[9] */
static uint32 ver_count = 0;        /* 0x94c4  EXT_ID reply[10] */
static int32  ode_units = 0;        /* 0x94c8 */
static uint32 ode_unit = 0;         /* 0x94cc  1 when the xbus has 2 units, else 0 */
static Item   ode_rwreq = 0;        /* 0x94d0  "oderw" IOReq, created once */

static uint8  ode_xfer[ODE_XFER_SIZE];   /* LaunchMe ZI 0x978c */

int32 ode_count_units(void);
int32 ode_cmd(uint8 *cmd, uint8 *reply, int32 replylen);
int32 ramp_read(int32 unit, uint32 addr, void *buf, int32 len);

/* 0x18d0 */
uint32 ode_get_ext_id_b1(void)
{
    return ext_id_b1;
}

/* 0x18f0 */
uint32 ode_get_version(void)
{
    ver_major &= 0xff;
    ver_minor &= 0xff;
    ver_count &= 0xff;
    return (ver_count << 16) | (ver_major << 8) | ver_minor;
}

/* 0x1964: 93 EXT_ID */
void ode_refresh_ext_id(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[12];
    int32 err;
    uint32 i;

    cmd[0] = 0x93;
    err = ode_cmd(cmd, reply, 12);
    if (err != 0)
        return;
    memset(ode_name, 0, 16);
    for (i = 2; reply[i] >= 0x20; i++) {
        if (i >= 14 || i >= 12)
            break;
        ode_name[i - 2] = reply[i];
    }
    ver_major = reply[8];
    ver_minor = reply[9];
    ver_count = reply[10];
}

/* 0x1a3c */
char *ode_init(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[12];
    uint8 buf[32];
    int32 err;
    uint32 i;

    ode_dev = FindAndOpenDevice("ode");
    memset(ode_name, 0, 16);
    if (ode_dev < 0) {
        kprintf("Failed to open ode device\n");
        return 0;
    }
    ramp_dev = FindAndOpenDevice("ramp");
    ode_units = ode_count_units();
    kprintf("Total of %d units\ndone\n", ode_units);
    if (ode_units == 2)
        ode_unit = 1;
    else
        ode_unit = 0;
    cmd[0] = 0x93;
    err = ode_cmd(cmd, reply, 12);
    kprintf("stat res = %x\n", err);
    PrintfSysErr(err);
    ext_id_b1 = reply[1];
    for (i = 2; reply[i] >= 0x20; i++) {
        if (i >= 14 || i >= 12)
            break;
        ode_name[i - 2] = reply[i];
    }
    ver_major = reply[8];
    ver_minor = reply[9];
    ver_count = reply[10];
    ramp_read(0, 0, buf, 0x20);
    for (i = 0; i < 0x20; i++) {
        if (i != 0 && (i & 0xf) == 0)
            printf("\n");
        printf("%2x ", buf[i]);
    }
    return ode_name;
}

/* 0x1c7c: ode-device command 13 = xbus dev_MaxUnitNum; returns it + 1 */
int32 ode_count_units(void)
{
    IOInfo ioi = {0};
    int32 n;
    Item req;
    Err err;

    req = CreateIOReq("odecmd0", 0xff, ode_dev, 0);
    if (req < 0) {
        printf("Failed to create iorq\n");
        return req;
    }
    ioi.ioi_Command = 13;
    ioi.ioi_User = 0;
    ioi.ioi_Recv.iob_Buffer = &n;
    ioi.ioi_Recv.iob_Len = 4;
    ioi.ioi_Send.iob_Buffer = 0;
    ioi.ioi_Send.iob_Len = 0;
    ioi.ioi_Flags = IO_QUICK;
    err = DoIO(req, &ioi);
    DeleteItem(req);
    if (err == 0)
        return n + 1;
    return 1;
}

/* 0x1d78: send a 7-byte command, receive replylen status bytes */
int32 ode_cmd(uint8 *cmd, uint8 *reply, int32 replylen)
{
    IOInfo ioi = {0};
    Item req;
    Err err;

    req = CreateIOReq("odecmd0", 0xff, ode_dev, 0);
    if (req < 0) {
        printf("Failed to create iorq\n");
        return req;
    }
    ioi.ioi_Command = 3;
    ioi.ioi_User = ode_unit;
    ioi.ioi_Recv.iob_Buffer = reply;
    ioi.ioi_Recv.iob_Len = replylen;
    ioi.ioi_Send.iob_Buffer = cmd;
    ioi.ioi_Send.iob_Len = 7;
    ioi.ioi_Flags = IO_QUICK;
    err = DoIO(req, &ioi);
    DeleteItem(req);
    return err;
}

/* 0x1e44: command + data phase (ode-device command 12) */
int32 ode_cmd_data(uint8 *cmd, uint8 *reply, int32 replylen, uint8 *data, int32 datalen)
{
    IOInfo ioi = {0};
    IOInfo ioi2 = {0};
    OdeRW rw;
    Item req;
    Err err;
    int32 i;

    req = CreateIOReq("odecmd1", 200, ode_dev, 0);
    if (req < 0) {
        printf("Failed to create iorq\n");
        return req;
    }
    if (ode_rwreq <= 0) {
        ode_rwreq = CreateIOReq("oderw", 0xff, ode_dev, 0);
        if (ode_rwreq < 0) {
            printf("Failed to create rw iorq\n");
            return ode_rwreq;
        }
    }
    ioi.ioi_Command = 12;
    ioi.ioi_User = ode_unit;
    ioi.ioi_Recv.iob_Buffer = reply;
    ioi.ioi_Recv.iob_Len = replylen;
    ioi.ioi_Send.iob_Buffer = cmd;
    ioi.ioi_Send.iob_Len = 7;
    ioi.ioi_User = (uint32)&rw;
    rw.unit = ode_unit;
    rw.buf = data;
    rw.len = datalen;
    err = DoIO(req, &ioi);
    printf("cmd %x\n", err);
    PrintfSysErr(err);
    for (i = 0; i < 16; i++)
        printf("%x ", data[i]);
    DeleteItem(req);
    return err;
}

/* 0x2014: C0 change TOC */
int32 ode_c0_change_toc(uint32 toc_id)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xc0;
    cmd[1] = toc_id >> 24;
    cmd[2] = toc_id >> 16;
    cmd[3] = toc_id >> 8;
    cmd[4] = toc_id;
    err = ode_cmd(cmd, reply, 2);
    return err;
}

/* 0x2090: C3 clear playlist */
int32 ode_c3_clear_playlist(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xc3;
    err = ode_cmd(cmd, reply, 2);
    return err;
}

/* 0x20e8: C4 add to playlist */
int32 ode_c4_add_playlist(uint32 toc_id)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xc4;
    cmd[1] = toc_id >> 24;
    cmd[2] = toc_id >> 16;
    cmd[3] = toc_id >> 8;
    cmd[4] = toc_id;
    err = ode_cmd(cmd, reply, 4);
    if (err >= 0 && (reply[1] == 0 || reply[2] == 0))
        err = -1;
    return err;
}

/* 0x2188: C5 launch playlist */
int32 ode_c5_launch_playlist(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xc5;
    err = ode_cmd(cmd, reply, 2);
    return err;
}

/* 0x21e0: E6 fill the ODE buffer, 4 bytes per command */
int32 ode_e6_fill_buffer(uint8 *buf, uint32 len)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;
    uint32 i;
    uint32 w;
    int32 unused;

    if (len > ODE_XFER_SIZE)
        return -1;
    if (buf == 0)
        return -1;
    cmd[0] = 0xe6;
    for (i = 0; i < len; i += 4) {
        w = i >> 2;
        cmd[1] = (w >> 8) & 0xff;
        cmd[2] = w & 0xff;
        cmd[3] = buf[i];
        cmd[4] = buf[i + 1];
        cmd[5] = buf[i + 2];
        cmd[6] = buf[i + 3];
        err = ode_cmd(cmd, reply, 2);
        if (err < 0) {
            printf("filling error %x\n", err);
            return err;
        }
    }
    return err;
}

/* 0x22f0: E1 open file (by name via the buffer, or by TOC id when name == NULL) */
int32 ode_e1_open_file(char *name, uint32 toc_id, uint32 mode)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err = 0;
    uint32 len;

    cmd[0] = 0xe1;
    cmd[6] = mode;
    if (name != 0) {
        len = strlen(name) + 1;
        err = ode_e6_fill_buffer((uint8 *)name, len);
        if (err < 0)
            goto out;
        cmd[2] = len >> 16;
        cmd[3] = len;
        cmd[1] = 0;
    } else {
        cmd[1] = 1;
        cmd[2] = toc_id >> 24;
        cmd[3] = toc_id >> 16;
        cmd[4] = toc_id >> 8;
        cmd[5] = toc_id;
    }
    err = ode_cmd(cmd, reply, 3);
    if (err < 0)
        goto out;
    if (reply[1] == 0)
        err = -1;
out:
    return err;
}

/* 0x23f0: E2 (unused) */
int32 ode_e2_seek_file(int32 x)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xe2;
    cmd[1] = x >> 8;
    cmd[2] = x & 0xff;
    err = ode_cmd(cmd, reply, 2);
    return err;
}

/* 0x245c */
int32 ode_fill_buffer(uint8 *buf, uint32 len)
{
    return ode_e6_fill_buffer(buf, len);
}

/* 0x248c: E7 read the ODE buffer */
int32 ode_e7_read_buffer(uint8 *dst, uint32 len)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err = -1;

    if (dst == 0)
        goto out;
    cmd[0] = 0xe7;
    if (len > ODE_XFER_SIZE)
        len = ODE_XFER_SIZE;
    err = ode_cmd_data(cmd, reply, 2, ode_xfer, ODE_XFER_SIZE);
    if (err != 0)
        goto out;
    if (reply[0] != 0xe7)
        err = -1;
    else
        memcpy(dst, ode_xfer, len);
out:
    return err;
}

/* 0x254c: E3 read file into the buffer; *n = bytes read */
int32 ode_e3_read_file(uint32 len, uint32 *n)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    if (n == 0)
        return -1;
    *n = 0;
    cmd[0] = 0xe3;
    cmd[1] = len >> 8;
    cmd[2] = len & 0xff;
    err = ode_cmd(cmd, reply, 4);
    if (err == 0)
        *n = (reply[1] << 8) | reply[2];
    return err;
}

/* 0x25ec: E4 write file from the buffer; *n = bytes written */
int32 ode_e4_write_file(uint32 len, uint32 *n)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    if (n == 0)
        return -1;
    *n = 0;
    cmd[0] = 0xe4;
    cmd[1] = len >> 8;
    cmd[2] = len & 0xff;
    err = ode_cmd(cmd, reply, 4);
    if (err == 0)
        *n = (reply[1] << 8) | reply[2];
    return err;
}

/* 0x268c: E5 close file */
int32 ode_e5_close_file(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xe5;
    err = ode_cmd(cmd, reply, 4);
    return err;
}

/* 0x26e4: F0 update ODE firmware; 1 when reply[1] == 0 */
int32 ode_f0_update_ode(void)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    cmd[0] = 0xf0;
    err = ode_cmd(cmd, reply, 3);
    if (err == 0)
        err = reply[1] == 0;
    return err;
}

/* 0x2760: C1 get TOC entry, 16 bytes per command (unused) */
int32 ode_c1_get_toc(uint32 toc_id, uint8 *out)
{
    uint8 cmd[8] = {0};
    uint8 reply[0x14];
    int32 off = 0;
    int32 total = 0;
    int32 *hdr;
    int32 err;
    int32 unused;

    memset(out, 0xff, 0x2c);
    do {
        cmd[0] = 0xc1;
        cmd[1] = toc_id >> 24;
        cmd[2] = toc_id >> 16;
        cmd[3] = toc_id >> 8;
        cmd[4] = toc_id;
        cmd[5] = off >> 8;
        cmd[6] = off;
        err = ode_cmd(cmd, reply, 0x12);
        if (reply[0] != 0xc1)
            return -1;
        memcpy(ode_xfer + off, &reply[1], 16);
        if (off == 0) {
            hdr = (int32 *)ode_xfer;
            total = hdr[2] + 13;
            if (hdr[0] == -1)
                total = 0;
        }
        off += 16;
    } while (off < total);
    if (total != 0)
        memcpy(out, ode_xfer, total);
    return err;
}

/* 0x2878: D1 get TOC list page */
int32 ode_d1_get_toc_list(uint32 toc_id, uint32 index, uint8 *out, int32 len)
{
    uint8 cmd[8] = {0};
    uint8 reply[4];
    int32 err;

    memset(out, 0xff, len);
    cmd[0] = 0xd1;
    cmd[1] = toc_id >> 24;
    cmd[2] = toc_id >> 16;
    cmd[3] = toc_id >> 8;
    cmd[4] = toc_id;
    cmd[5] = index >> 8;
    cmd[6] = index;
    err = ode_cmd_data(cmd, reply, 2, ode_xfer, ODE_XFER_SIZE);
    if (reply[0] != 0xd1)
        return -1;
    memcpy(out, ode_xfer, len);
    return err;
}

/* 0x2954: read len bytes at addr from ramp unit (0 = memory, 3 = NVRAM, ...) */
int32 ramp_read(int32 unit, uint32 addr, void *buf, int32 len)
{
    IOInfo ioi = {0};
    int32 u;
    Err err;
    Item req;

    req = CreateIOReq("ramprq", 0xff, ramp_dev, 0);
    if (req < 0) {
        printf("Failed to create iorq\n");
        return req;
    }
    u = unit;
    ioi.ioi_Command = CMD_READ;
    ioi.ioi_Recv.iob_Buffer = buf;
    ioi.ioi_Recv.iob_Len = len;
    ioi.ioi_Send.iob_Buffer = 0;
    ioi.ioi_Send.iob_Len = 0;
    ioi.ioi_Offset = addr;
    ioi.ioi_User = (uint32)&u;
    err = DoIO(req, &ioi);
    if (u != 0)
        err = ioi.ioi_User;
    if (err < 0)
        ;
    PrintfSysErr(err);
    DeleteItem(req);
    return err;
}

/* 0x2a54: write len bytes at addr to ramp unit */
int32 ramp_write(int32 unit, uint32 addr, void *buf, int32 len)
{
    IOInfo ioi = {0};
    int32 u;
    Err err;
    Item req;

    req = CreateIOReq("ramprq", 0xff, ramp_dev, 0);
    if (req < 0) {
        printf("Failed to create iorq\n");
        return req;
    }
    u = unit;
    ioi.ioi_Command = CMD_WRITE;
    ioi.ioi_Recv.iob_Buffer = 0;
    ioi.ioi_Recv.iob_Len = 0;
    ioi.ioi_Send.iob_Buffer = buf;
    ioi.ioi_Send.iob_Len = len;
    ioi.ioi_Offset = addr;
    ioi.ioi_User = (uint32)&u;
    err = DoIO(req, &ioi);
    if (u != 0)
        err = ioi.ioi_User;
    if (err < 0)
        ;
    PrintfSysErr(err);
    DeleteItem(req);
    return err;
}

/* 0x2b44 */
Item nvram_unmount(void)
{
    Item r = 0;

    r = LoadProgram("$c/lmadm -u ram 3 0 nvram");
    return r;
}

/* 0x2b90 */
Item run_fscheck(void)
{
    Item r = 0;

    r = LoadProgram("$c/fscheck");
    return r;
}
