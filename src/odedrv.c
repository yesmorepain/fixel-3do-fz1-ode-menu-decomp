/* odedrv.c - reconstructed from System/Drivers/ode 0x1f0-0x1060 (see docs/objects.md).
 * Privileged task that creates two Portfolio devices:
 *   "ode"  - proxy to the "xbus" device (cmd 13 = max unit, cmd 12 = data phase + command phase, others passed on)
 *   "ramp" - unit 0 = supervisor memory read/write at ioi_Offset, other units forwarded to the "ram" device
 * File name, identifier names and parameter types are guesses; the code bytes are what is matched.
 */
#include "types.h"
#include "nodes.h"
#include "kernelnodes.h"
#include "item.h"
#include "list.h"
#include "task.h"
#include "io.h"
#include "device.h"
#include "driver.h"
#include "string.h"

/* kprintf is called through the swi.lib stub (no __swi declaration from debug.h) */
extern void kprintf(const char *fmt, ...);
extern void PrintfSysErr(Item err);

/* kernellib supervisor calls (not in the public SDK headers) */
extern Item SuperFindNamedItem(int32 ctype, char *name);
extern Item SuperCreateItem(int32 ctype, TagArg *p);
extern Item SuperOpenItem(Item foundItem, void *args);
extern Err  SuperInternalSendIO(IOReq *ior);
extern Err  SuperInternalDoIO(IOReq *ior);
extern void SuperInternalAbortIO(IOReq *ior);
extern void Superkprintf(const char *fmt, ...);

/* driver/device creation tags (Portfolio-internal; names are guesses, values from the binary) */
#define CREATEDRIVER_TAG_ABORTIO   (TAG_ITEM_LAST+1)   /* 10 */
#define CREATEDRIVER_TAG_MAXCMDS   (TAG_ITEM_LAST+2)   /* 11 */
#define CREATEDRIVER_TAG_CMDTABLE  (TAG_ITEM_LAST+3)   /* 12 */
#define CREATEDRIVER_TAG_DISPATCH  (TAG_ITEM_LAST+6)   /* 15 */
#define CREATEDEVICE_TAG_DRVR      (TAG_ITEM_LAST+1)   /* 10 */
#define CREATEDEVICE_TAG_INIT      (TAG_ITEM_LAST+7)   /* 16 */

#define ERR_BADUNIT  0xd57b9010
#define ERR_BADPTR   0xd57b9009

/* ode device cmd 12 argument (ioi_User): bit 31 = write, low byte = xbus unit */
typedef struct OdeRW {
    uint32  unit;
    void   *buf;
    int32   len;
} OdeRW;

static void  ode_abortio(IOReq *ior);
static int32 ode_dispatch(IOReq *ior);
static Item  xbus_proxy_init(Device *dev);
static int32 ramp_cmd_write(IOReq *ior);
static int32 ramp_cmd_read(IOReq *ior);
static int32 ramp_cmd_status(IOReq *ior);
static Item  ramp_init(Device *dev);
static void  empty_func(IOReq *ior);
static IOReq *xb_callback(IOReq *ior);

/* RW 0x19b4 */
static TagArg drv_tags[] = {
    TAG_ITEM_PRI,              (void *)1,
    TAG_ITEM_NAME,             (void *)"ode",
    CREATEDRIVER_TAG_ABORTIO,  (void *)ode_abortio,
    CREATEDRIVER_TAG_MAXCMDS,  (void *)14,
    CREATEDRIVER_TAG_DISPATCH, (void *)ode_dispatch,
    TAG_END,                   0,
};
/* RW 0x19e4 */
static TagArg dev_tags[] = {
    TAG_ITEM_PRI,              (void *)3,
    CREATEDEVICE_TAG_DRVR,     (void *)1,
    TAG_ITEM_NAME,             (void *)"ode",
    CREATEDEVICE_TAG_INIT,     (void *)xbus_proxy_init,
    TAG_END,                   0,
};
/* RW 0x1a0c */
static int32 (*ramp_cmds[])(IOReq *) = {
    ramp_cmd_write,
    ramp_cmd_read,
    ramp_cmd_status,
};
/* RW 0x1a18 */
static TagArg ramdrv_tags[] = {
    TAG_ITEM_PRI,              (void *)1,
    TAG_ITEM_NAME,             (void *)"ramp",
    CREATEDRIVER_TAG_ABORTIO,  (void *)empty_func,
    CREATEDRIVER_TAG_MAXCMDS,  (void *)3,
    CREATEDRIVER_TAG_CMDTABLE, (void *)ramp_cmds,
    TAG_END,                   0,
};
/* RW 0x1a48 */
static TagArg ramdev_tags[] = {
    TAG_ITEM_PRI,              (void *)1,
    CREATEDEVICE_TAG_DRVR,     (void *)0,
    TAG_ITEM_NAME,             (void *)"ramp",
    CREATEDEVICE_TAG_INIT,     (void *)ramp_init,
    TAG_END,                   0,
};

static IOReq *xbus_cmd_ior = 0;     /* 0x1a70 */
static IOReq *xbus_rw_ior = 0;      /* 0x1a74 */
static Item   xbus_dev = 0;         /* 0x1a78 */
static IOReq *ram_ior = 0;          /* 0x1a7c */
static Item   ram_item = 0;         /* 0x1a80  ram device, then the ram IOReq */
static Item   xbus_cmd_item = 0;    /* 0x1a84 */
static Item   xbus_rw_item = 0;     /* 0x1a88 */
static Item   ode_dev = 0;          /* 0x1a8c */

int main(int argc, char **argv)
{
    Item   drv;
    Item   dev = -1;
    Item   ramdrv;
    Item   ramdev = -1;
    int32  sig;
    int32  sigs;

    kprintf("Starting xbus proxy\n");
    drv = CreateItem(MKNODEID(KERNELNODE, DRIVERNODE), drv_tags);
    if (drv > 0) {
        kprintf("Drv created %x\n", drv);
        dev_tags[1].ta_Arg = (void *)drv;
        dev = CreateItem(MKNODEID(KERNELNODE, DEVICENODE), dev_tags);
        if (dev < 0) {
            PrintfSysErr(dev);
            DeleteItem(drv);
            return 0;
        }
    } else {
        PrintfSysErr(drv);
        return 0;
    }
    ramdrv = CreateItem(MKNODEID(KERNELNODE, DRIVERNODE), ramdrv_tags);
    if (ramdrv > 0) {
        kprintf("RamDrv created %x\n", ramdrv);
        ramdev_tags[1].ta_Arg = (void *)ramdrv;
        ramdev = CreateItem(MKNODEID(KERNELNODE, DEVICENODE), ramdev_tags);
        if (ramdev < 0) {
            kprintf("ramdev : ");
            PrintfSysErr(ramdev);
            DeleteItem(ramdev);     /* sic: the failed device, not ramdrv */
            return 0;
        }
    } else {
        kprintf("ramdrv : ");
        PrintfSysErr(ramdrv);
        return 0;
    }
    kprintf("Driver launched drv %x dev %x\n", drv, dev);
    ode_dev = dev;
    sig = AllocSignal(0);
    if (sig <= 0) {
        kprintf("cannot alloc signal\n");
        return 0;
    }
    for (;;) {
        sigs = WaitSignal(sig);
        if (sigs & SIGF_ABORT) {
            kprintf("Abt\n");
            return 0;
        }
    }
    return 0;
}

/* three identical unreferenced stubs */
static int32 dev_stub_ret0_a(Device *dev)
{
    return 0;
}

static int32 dev_stub_ret0_b(Device *dev)
{
    return 0;
}

static int32 dev_stub_ret0_c(Device *dev)
{
    return 0;
}

static Item xbus_proxy_init(Device *dev)
{
    TagArg  tags[2];
    uint8   unused[0x48];
    Item    xbus;
    uint32 *madam;
    uint8   maxunit;
    int32   unused1;
    int32   unused2;

    Superkprintf("In a privileged mode\n");
    madam = (uint32 *)0x3300000;
    Superkprintf("%8x %8x %8x\n", madam[0], madam[1], madam[2]);
    dev->dev_OpenCnt = 0;
    xbus = SuperFindNamedItem(MKNODEID(KERNELNODE, DEVICENODE), "xbus");
    if (xbus < 0) {
        Superkprintf("no xbus item %x\n", xbus);
        goto out;
    }
    xbus_dev = SuperOpenItem(xbus, 0);
    if (xbus_dev < 0) {
        Superkprintf("unable to open xbus item %x\n", xbus);
        goto out;
    }
    tags[0].ta_Tag = CREATEIOREQ_TAG_DEVICE;
    tags[0].ta_Arg = (void *)xbus_dev;
    tags[1].ta_Tag = TAG_END;
    xbus_cmd_item = SuperCreateItem(MKNODEID(KERNELNODE, IOREQNODE), tags);
    if (xbus_cmd_item < 0) {
        Superkprintf("unable to create xbus rqitem %x\n", xbus);
        goto out;
    }
    xbus_cmd_ior = (IOReq *)LookupItem(xbus_cmd_item);
    xbus_rw_item = SuperCreateItem(MKNODEID(KERNELNODE, IOREQNODE), tags);
    if (xbus_rw_item < 0) {
        Superkprintf("unable to create xbus rw rqitem %x\n", xbus);
        goto out;
    }
    xbus_rw_ior = (IOReq *)LookupItem(xbus_rw_item);
    maxunit = ((Device *)LookupItem(xbus_dev))->dev_MaxUnitNum;
out:
    return dev->dev.n_Item;
}

static void ode_abortio(IOReq *ior)
{
    IOReq *xior;

    if (ior->io_Info.ioi_Command < 2)
        xior = xbus_rw_ior;
    else
        xior = xbus_cmd_ior;
    Superkprintf("abort1 %x\n", xior);
    SuperInternalAbortIO(xior);
    Superkprintf("abort2 %x\n", xior);
    return;
}

static int32 ode_dispatch(IOReq *ior)
{
    int32   err;
    IOReq  *xior;
    OdeRW  *rw;
    uint8   maxunit;
    uint32  unused[4];

    if (ior->io_Info.ioi_Command == 13) {
        maxunit = ((Device *)LookupItem(xbus_dev))->dev_MaxUnitNum;
        if (ior->io_Info.ioi_Recv.iob_Buffer != 0) {
            *(uint32 *)ior->io_Info.ioi_Recv.iob_Buffer = maxunit;
            ior->io_Actual = 4;
        } else
            ior->io_Actual = 0;
        return 0;
    }
    if (ior->io_Info.ioi_Command == 12) {
        rw = (OdeRW *)ior->io_Info.ioi_User;
        if (rw->unit & 0x80000000) {
            xbus_rw_ior->io_Info.ioi_Command = CMD_WRITE;
            xbus_rw_ior->io_Info.ioi_Unit = rw->unit & 0xff;
            xbus_rw_ior->io_Info.ioi_Send.iob_Buffer = rw->buf;
            xbus_rw_ior->io_Info.ioi_Send.iob_Len = rw->len;
            xbus_rw_ior->io_Info.ioi_Recv.iob_Buffer = 0;
            xbus_rw_ior->io_Info.ioi_Recv.iob_Len = 0;
            xbus_rw_ior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
            xbus_rw_ior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
            xbus_rw_ior->io_CallBack = xb_callback;
        } else {
            xbus_rw_ior->io_Info.ioi_Command = CMD_READ;
            xbus_rw_ior->io_Info.ioi_Unit = rw->unit & 0xff;
            xbus_rw_ior->io_Info.ioi_Send.iob_Buffer = 0;
            xbus_rw_ior->io_Info.ioi_Send.iob_Len = 0;
            xbus_rw_ior->io_Info.ioi_Recv.iob_Buffer = rw->buf;
            xbus_rw_ior->io_Info.ioi_Recv.iob_Len = rw->len;
            xbus_rw_ior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
            xbus_rw_ior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
            xbus_rw_ior->io_CallBack = xb_callback;
        }
        err = SuperInternalSendIO(xbus_rw_ior);
        if (err != 0 || (err = xbus_rw_ior->io_Error) != 0) {
            Superkprintf("Data phase send err=%x\n", err);
            return err;
        }
        xbus_cmd_ior->io_Info.ioi_Command = 3;     /* xbus CmdCommand */
        xbus_cmd_ior->io_Info.ioi_Unit = rw->unit & 0xff;
        xbus_cmd_ior->io_Info.ioi_Send.iob_Buffer = ior->io_Info.ioi_Send.iob_Buffer;
        xbus_cmd_ior->io_Info.ioi_Send.iob_Len = ior->io_Info.ioi_Send.iob_Len;
        xbus_cmd_ior->io_Info.ioi_Recv.iob_Buffer = ior->io_Info.ioi_Recv.iob_Buffer;
        xbus_cmd_ior->io_Info.ioi_Recv.iob_Len = ior->io_Info.ioi_Recv.iob_Len;
        xbus_cmd_ior->io_Info.ioi_Offset = 0;
        xbus_cmd_ior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
        xbus_cmd_ior->io_CallBack = xb_callback;
        err = SuperInternalSendIO(xbus_cmd_ior);
        if (err < 0) {
            Superkprintf("Cmd phase send err=%x\n", err);
            return err;
        }
        if (err == 0) {
            Superkprintf("cmd wait start\n");
            while (!(xbus_cmd_ior->io_Flags & IO_DONE))
                ;
            Superkprintf("cmd wait end\n");
            Superkprintf("data wait start\n");
            while (!(xbus_rw_ior->io_Flags & IO_DONE))
                ;
            Superkprintf("data wait end\n");
        }
        if (err < 0 || (err = xbus_cmd_ior->io_Error) != 0) {
            Superkprintf("Cmd phase err=%x\n", err);
            return err;
        }
        ior->io_Actual = xbus_rw_ior->io_Actual;
        return err;
    }
    if (ior->io_Info.ioi_Command < 2)
        xior = xbus_rw_ior;
    else
        xior = xbus_cmd_ior;
    xior->io_Info.ioi_Command = ior->io_Info.ioi_Command;
    xior->io_Info.ioi_Unit = ior->io_Info.ioi_User;
    xior->io_Info.ioi_Send.iob_Buffer = ior->io_Info.ioi_Send.iob_Buffer;
    xior->io_Info.ioi_Send.iob_Len = ior->io_Info.ioi_Send.iob_Len;
    xior->io_Info.ioi_Recv.iob_Buffer = ior->io_Info.ioi_Recv.iob_Buffer;
    xior->io_Info.ioi_Recv.iob_Len = ior->io_Info.ioi_Recv.iob_Len;
    xior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
    xior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
    xior->io_CallBack = xb_callback;
    err = SuperInternalSendIO(xior);
    ior->io_Error = xior->io_Error;
    if (err < 0) {
        Superkprintf("send io err %x\n", err);
        return err;
    }
    if (err == 0)
        while (!(xior->io_Flags & IO_DONE))
            ;
    ior->io_Actual = xior->io_Actual;
    return err;
}

static int32 ramp_cmd_write(IOReq *ior)
{
    IOReq  *rior;
    int32  *user;
    int32   err;
    uint32 *p;
    uint32 *addr;
    uint32  tmp;

    rior = ram_ior;
    if (ior->io_Info.ioi_Unit != 0 && ior->io_Info.ioi_Unit != 3) {
        ior->io_Error = ERR_BADUNIT;
        return 1;
    }
    user = (int32 *)ior->io_Info.ioi_User;
    if (user == 0)
        return ERR_BADPTR;
    rior->io_Info.ioi_Command = ior->io_Info.ioi_Command;
    rior->io_Info.ioi_Unit = *user;
    rior->io_Info.ioi_Send.iob_Buffer = ior->io_Info.ioi_Send.iob_Buffer;
    rior->io_Info.ioi_Send.iob_Len = ior->io_Info.ioi_Send.iob_Len;
    rior->io_Info.ioi_Recv.iob_Buffer = 0;
    rior->io_Info.ioi_Recv.iob_Len = 0;
    rior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
    rior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
    if (rior->io_Info.ioi_Unit != 0) {
        err = SuperInternalDoIO(rior);
        *user = rior->io_Error;
    } else {
        err = 0;
        addr = (uint32 *)ior->io_Info.ioi_Offset;
        if (ior->io_Info.ioi_Send.iob_Len == 4) {
            memmove(&tmp, ior->io_Info.ioi_Send.iob_Buffer, ior->io_Info.ioi_Send.iob_Len);
            p = (uint32 *)ior->io_Info.ioi_Offset;
            *p = tmp;
        } else
            memmove(addr, ior->io_Info.ioi_Send.iob_Buffer, ior->io_Info.ioi_Send.iob_Len);
        rior->io_Actual = ior->io_Info.ioi_Send.iob_Len;
    }
    ior->io_Actual = rior->io_Actual;
    return err;
}

static int32 ramp_cmd_read(IOReq *ior)
{
    IOReq  *rior;
    int32  *user;
    int32   err;
    uint32 *p;
    uint32 *addr;
    uint32  tmp;

    rior = ram_ior;
    user = (int32 *)ior->io_Info.ioi_User;
    if (user == 0)
        return ERR_BADPTR;
    rior->io_Info.ioi_Command = ior->io_Info.ioi_Command;
    rior->io_Info.ioi_Unit = *user;
    rior->io_Info.ioi_Send.iob_Buffer = 0;
    rior->io_Info.ioi_Send.iob_Len = 0;
    rior->io_Info.ioi_Recv.iob_Buffer = ior->io_Info.ioi_Recv.iob_Buffer;
    rior->io_Info.ioi_Recv.iob_Len = ior->io_Info.ioi_Recv.iob_Len;
    rior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
    rior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
    if (rior->io_Info.ioi_Unit != 0) {
        err = SuperInternalDoIO(rior);
        *user = rior->io_Error;
    } else {
        err = 0;
        addr = (uint32 *)ior->io_Info.ioi_Offset;
        if (ior->io_Info.ioi_Recv.iob_Len == 4) {
            p = (uint32 *)ior->io_Info.ioi_Offset;
            tmp = *p;
            memmove(ior->io_Info.ioi_Recv.iob_Buffer, &tmp, ior->io_Info.ioi_Recv.iob_Len);
        } else
            memmove(ior->io_Info.ioi_Recv.iob_Buffer, addr, ior->io_Info.ioi_Recv.iob_Len);
        rior->io_Actual = ior->io_Info.ioi_Recv.iob_Len;
    }
    ior->io_Actual = rior->io_Actual;
    return err;
}

static int32 ramp_cmd_status(IOReq *ior)
{
    IOReq  *rior;
    int32  *user;   /* sic: never assigned */
    int32   err;

    rior = ram_ior;
    if (user == 0)
        return ERR_BADPTR;
    rior->io_Info.ioi_Command = ior->io_Info.ioi_Command;
    rior->io_Info.ioi_Unit = *user;
    rior->io_Info.ioi_Send.iob_Buffer = 0;
    rior->io_Info.ioi_Send.iob_Len = 0;
    rior->io_Info.ioi_Recv.iob_Buffer = ior->io_Info.ioi_Recv.iob_Buffer;
    rior->io_Info.ioi_Recv.iob_Len = ior->io_Info.ioi_Recv.iob_Len;
    rior->io_Info.ioi_Offset = ior->io_Info.ioi_Offset;
    rior->io_Info.ioi_Flags = ior->io_Info.ioi_Flags;
    err = SuperInternalDoIO(rior);
    *user = rior->io_Error;
    ior->io_Actual = rior->io_Actual;
    return err;
}

static Item ramp_init(Device *dev)
{
    TagArg  tags[2];
    uint8   unused[0x44];
    Item    ram;
    int32   unused1;
    int32   unused2;
    int32   unused3;

    Superkprintf("In a privileged mode\n");
    dev->dev_OpenCnt = 0;
    ram = SuperFindNamedItem(MKNODEID(KERNELNODE, DEVICENODE), "ram");
    if (ram < 0) {
        Superkprintf("no ram item %x\n", ram);
        goto out;
    }
    ram_item = SuperOpenItem(ram, 0);
    if (ram_item < 0) {
        Superkprintf("unable to open ram item %x\n", ram);
        goto out;
    }
    tags[0].ta_Tag = CREATEIOREQ_TAG_DEVICE;
    tags[0].ta_Arg = (void *)ram_item;
    tags[1].ta_Tag = TAG_END;
    ram_item = SuperCreateItem(MKNODEID(KERNELNODE, IOREQNODE), tags);
    if (ram_item < 0) {
        Superkprintf("unable to create ram rqitem %x\n", ram);
        goto out;
    }
    ram_ior = (IOReq *)LookupItem(ram_item);
out:
    return dev->dev.n_Item;
}

static void empty_func(IOReq *ior)
{
}

static IOReq *xb_callback(IOReq *ior)
{
    Superkprintf("xb callback\n");
    ior->io_Info.ioi_Flags |= 1;
    return 0;
}
