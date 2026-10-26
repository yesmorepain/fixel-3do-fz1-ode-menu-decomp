// Apply hand names from a TSV (addr<TAB>name<TAB>conf<TAB>comment; '#' comments). Creates the function if missing;
// the comment (prefixed with [C]/[L]) becomes the function's plate comment. Arg: TSV path.
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path p = Paths.get(getScriptArgs()[0]);
        if (!Files.exists(p)) { println("no names file " + p); return; }
        int n = 0;
        for (String l : Files.readAllLines(p)) {
            if (l.isBlank() || l.startsWith("#")) continue;
            String[] c = l.split("\t");
            Address a = toAddr(Long.parseLong(c[0], 16));
            Function f = getFunctionAt(a);
            if (f == null) { disassemble(a); f = createFunction(a, c[1]); }
            if (f == null) { println("cannot create " + l); continue; }
            f.setName(c[1], SourceType.USER_DEFINED);
            String conf = c.length > 2 ? c[2] : "L";
            f.setComment("[" + conf + "] " + (c.length > 3 ? c[3] : ""));
            n++;
        }
        println("applied " + n + " names from " + p);
    }
}
