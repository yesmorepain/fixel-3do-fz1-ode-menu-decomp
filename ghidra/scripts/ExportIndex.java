// Write a function index TSV: addr, size, name, callers, callees, strings (printable C strings referenced by the body).
// Arg: output path.
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ExportIndex extends GhidraScript {
    String cstr(Memory mem, Address a) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 120; i++) {
                int b = mem.getByte(a.add(i)) & 0xff;
                if (b == 0) break;
                if (b == '\n') { sb.append("\\n"); continue; }
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) b);
            }
        } catch (Exception e) { return null; }
        return sb.length() >= 3 ? sb.toString() : null;
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
            w.println("addr\tsize\tname\tcallers\tcallees\tstrings");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                Set<String> callers = new TreeSet<>(), callees = new TreeSet<>(), strs = new LinkedHashSet<>();
                for (Function c : f.getCallingFunctions(monitor)) callers.add(c.getName());
                for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getName());
                for (Address a : f.getBody().getAddresses(true)) {
                    for (Reference r : rm.getReferencesFrom(a)) {
                        Address t = r.getToAddress();
                        if (!t.isMemoryAddress()) continue;
                        String s = cstr(mem, t);
                        if (s == null) {   // literal pool word holding a pointer
                            try { s = cstr(mem, toAddr(mem.getInt(t) & 0xffffffffL)); } catch (Exception e) { }
                        }
                        if (s != null) strs.add("\"" + s + "\"");
                    }
                }
                w.printf("%x\t%d\t%s\t%s\t%s\t%s%n", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(),
                    f.getName(), String.join(",", callers), String.join(",", callees), String.join(" ", strs));
            }
        }
    }
}
