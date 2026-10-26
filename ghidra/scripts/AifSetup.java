// Pre-analysis setup for a 3DO AIF executable imported as raw binary at base 0:
// labels the AIF header fields, moves any file trailer (reloc list) to 0x80000000, creates the ZI (bss)
// block after RO+RW, marks the entry points. Note: 3DO ARM60 is big-endian (ARM:BE:32:v4).
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;

public class AifSetup extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Address base = toAddr(0);
        int ro = mem.getInt(toAddr(0x14)), rw = mem.getInt(toAddr(0x18)), zi = mem.getInt(toAddr(0x20));
        MemoryBlock b = mem.getBlock(base);
        b.setName("RO");
        if (rw > 0) {
            mem.split(b, toAddr(ro));
            MemoryBlock d = mem.getBlock(toAddr(ro));
            d.setName("RW");
            d.setExecute(false);
        }
        // Bytes past RO+RW (AIF self-relocation code + reloc list, run once at load, PIC) would overlap ZI:
        // move them to 0x80000000.
        if (mem.getBlock(toAddr(ro + rw)) != null) {
            MemoryBlock blk = mem.getBlock(toAddr(ro + rw));
            if (!blk.getStart().equals(toAddr(ro + rw))) { mem.split(blk, toAddr(ro + rw)); blk = mem.getBlock(toAddr(ro + rw)); }
            blk.setName("TRAILER");
            blk.setExecute(false); blk.setWrite(false);
            mem.moveBlock(blk, toAddr(0x80000000L), monitor);
        }
        if (zi > 0) {
            MemoryBlock z = mem.createUninitializedBlock("ZI", toAddr(ro + rw), zi, false);
            z.setRead(true); z.setWrite(true); z.setExecute(false);
        }
        String[] hdr = {"aif_decompress", "aif_selfreloc", "aif_zeroinit", "aif_entry", "aif_exit"};
        for (int i = 0; i < hdr.length; i++) createLabel(toAddr(4 * i), hdr[i], true, SourceType.IMPORTED);
        // word 3 = BL entry: target = 0xc + 8 + (imm24 << 2)
        int w = mem.getInt(toAddr(0xc));
        long entry = (0xc + 8 + ((w << 8) >> 6)) & 0xffffffffL;
        createLabel(toAddr(entry), "_main_entry", true, SourceType.IMPORTED);
        // word 1 BLs to RO+RW (self-reloc code, moved to 0x80000000; ZI at run time)
        createLabel(toAddr(ro + rw), "aif_selfreloc_at_load", true, SourceType.IMPORTED);
        addEntryPoint(base);
        createLabel(toAddr(0x80000000L), "aif_reloc_code", true, SourceType.IMPORTED);
        addEntryPoint(toAddr(entry));
        disassemble(base);
        disassemble(toAddr(entry));
        println(String.format("AIF ro=%#x rw=%#x zi=%#x entry=%#x", ro, rw, zi, entry));
    }
}
