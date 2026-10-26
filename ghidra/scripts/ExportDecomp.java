// Decompile every function to one C file (arg: output path).
// @category fz1menu
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "/tmp/decomp.c";
        DecompInterface di = new DecompInterface();
        DecompileOptions opt = new DecompileOptions();
        di.setOptions(opt);
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.openProgram(currentProgram);
        int n = 0, fail = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            w.println("/* FZ-1 ODE menu (boot.iso V5) - Ghidra 11.1.2 decompilation of all functions.");
            w.println("   Machine-generated pseudo-C: compiles only after cleanup. Addresses = original image. */\n");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                w.printf("/* ===== %s @ %s (%d bytes) ===== */%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
                if (r != null && r.decompileCompleted()) {
                    w.println(r.getDecompiledFunction().getC());
                    n++;
                } else {
                    w.println("/* decompile failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n");
                    fail++;
                }
            }
        }
        di.dispose();
        println("decompiled " + n + " functions, failed " + fail + " -> " + out);
    }
}
