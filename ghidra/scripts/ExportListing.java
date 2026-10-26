// Write an annotated disassembly listing: "<name>:" headers, address, bytes, instruction, refs, comments. Arg: path.
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ExportListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing l = currentProgram.getListing();
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
            for (CodeUnit cu : l.getCodeUnits(true)) {
                Function f = getFunctionAt(cu.getAddress());
                if (f != null) w.printf("%n%s:   ; @%s %s%n", f.getName(), f.getEntryPoint(), f.getComment() == null ? "" : f.getComment().replace("\n", " "));
                else {
                    Symbol s = st.getPrimarySymbol(cu.getAddress());
                    if (s != null && !s.isDynamic()) w.printf("%s:%n", s.getName());
                }
                String pre = cu.getComment(CodeUnit.PRE_COMMENT);
                if (pre != null) w.printf("        ; %s%n", pre.replace("\n", " "));
                StringBuilder refs = new StringBuilder();
                for (Reference r : cu.getReferencesFrom()) {
                    Symbol s = st.getPrimarySymbol(r.getToAddress());
                    if (s != null) refs.append(" ").append(s.getName());
                }
                String bytes = "";
                try { byte[] b = cu.getBytes(); StringBuilder sb = new StringBuilder();
                      for (int i = 0; i < Math.min(b.length, 4); i++) sb.append(String.format("%02x", b[i] & 0xff));
                      bytes = sb.toString(); } catch (Exception e) { }
                w.printf("%08x  %-8s  %-40s%s%n", cu.getAddress().getOffset(), bytes, cu.toString(),
                    refs.length() > 0 ? " ;" + refs : "");
            }
        }
    }
}
