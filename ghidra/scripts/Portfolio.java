// Post-analysis: create functions at every Norcroft APCS prologue (mov ip,sp; stmfd sp!,{..}) in RO,
// then name Portfolio SWIs from tools/swi_table.tsv: PRE comment at each SWI, and rename one-SWI stub functions.
// Arg: path to swi_table.tsv.
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.*;

public class Portfolio extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<Long, String> swi = new HashMap<>();
        for (String l : Files.readAllLines(Paths.get(getScriptArgs()[0]))) {
            String[] p = l.split("\t");
            swi.put(Long.decode(p[0]), p[1]);
        }
        Memory mem = currentProgram.getMemory();
        MemoryBlock ro = mem.getBlock("RO");
        int made = 0;
        for (long a = 0; a + 8 <= ro.getSize(); a += 4) {
            Address ad = toAddr(a);
            if (mem.getInt(ad) == 0xe1a0c00d && (mem.getInt(ad.add(4)) >>> 16) == 0xe92d
                    && getFunctionAt(ad) == null) {
                Function cont = getFunctionContaining(ad);
                if (cont != null) removeFunction(cont);
                disassemble(ad);
                if (createFunction(ad, null) != null) made++;
            }
        }
        analyzeChanges(currentProgram);
        int named = 0, sites = 0;
        for (Instruction in : currentProgram.getListing().getInstructions(true)) {
            int w = mem.getInt(in.getAddress());
            if ((w & 0x0f000000) != 0x0f000000) continue;
            long n = w & 0xffffffL;
            String nm = swi.getOrDefault(n, String.format("swi_%x", n));
            setPreComment(in.getAddress(), "SWI " + nm);
            sites++;
            Function f = getFunctionContaining(in.getAddress());
            if (f != null && f.getName().startsWith("FUN_") && f.getBody().getNumAddresses() <= 12 && swi.containsKey(n)) {
                f.setName(nm, SourceType.ANALYSIS);
                named++;
            }
        }
        println("prologue functions created " + made + ", SWI sites " + sites + ", stubs named " + named);
    }
}
