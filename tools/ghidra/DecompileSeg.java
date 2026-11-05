// Ghidra headless script (Java): create functions at known jump-table entry
// points, then decompile every function in the program to a .c file.
// Args: <outfile.c> <entriesfile>
// @category MacPortKit
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;

public class DecompileSeg extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outfile = args[0];
        String entriesfile = (args.length > 1) ? args[1] : null;
        FunctionManager fm = currentProgram.getFunctionManager();

        int created = 0;
        if (entriesfile != null && Files.exists(Paths.get(entriesfile))) {
            for (String line : Files.readAllLines(Paths.get(entriesfile))) {
                line = line.trim();
                if (line.isEmpty()) continue;
                long off = Long.parseLong(line, 16);
                Address addr = toAddr(off);
                try {
                    new DisassembleCommand(addr, null, true)
                        .applyTo(currentProgram, monitor);
                } catch (Exception e) { }
                if (fm.getFunctionAt(addr) == null) {
                    try {
                        Function f = createFunction(addr, null);
                        if (f != null) created++;
                    } catch (Exception e) { }
                }
            }
        }

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(new FileWriter(outfile));
        out.println("// Ghidra first-pass decompilation");
        out.println("// program: " + currentProgram.getName());
        out.println();
        int ok = 0, fail = 0;
        for (Function func : fm.getFunctions(true)) {
            Address ep = func.getEntryPoint();
            try {
                DecompileResults r = decomp.decompileFunction(func, 90, monitor);
                if (r != null && r.decompileCompleted()) {
                    out.println("/* ---- " + func.getName() + " @ " + ep + " ---- */");
                    out.println(r.getDecompiledFunction().getC());
                    out.println();
                    ok++;
                } else {
                    out.println("// FAILED decompile: " + func.getName() + " @ " + ep);
                    out.println();
                    fail++;
                }
            } catch (Exception e) {
                out.println("// EXCEPTION " + func.getName() + " @ " + ep + " : " + e);
                fail++;
            }
        }
        out.close();
        println("ENTRIES_CREATED=" + created + " DECOMP_OK=" + ok + " DECOMP_FAIL=" + fail);
    }
}
