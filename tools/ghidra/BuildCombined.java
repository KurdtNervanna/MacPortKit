// Ghidra headless: assemble all CODE segments + A5 world into one program so that
// cross-segment calls (via the synthesized loaded jump table) and A5-relative globals
// resolve. Then analyze and decompile everything to one .c file.
// Args: <layout.tsv> <entries.txt> <meta.tsv> <outfile.c> <baseDir>
// @category MacPortKit
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.address.AddressSet;
import java.io.ByteArrayInputStream;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class BuildCombined extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Path layout = Paths.get(a[0]), entries = Paths.get(a[1]), meta = Paths.get(a[2]);
        String outfile = a[3];
        Path base = Paths.get(a[4]);
        Memory mem = currentProgram.getMemory();

        int tx = currentProgram.startTransaction("build combined image");
        try {
            // --- create memory blocks from layout.tsv ---
            for (String line : Files.readAllLines(layout)) {
                if (line.trim().isEmpty()) continue;
                String[] p = line.split("\t");
                String name = p[0];
                Address addr = toAddr(Long.parseLong(p[1].substring(2), 16));
                long size = Long.parseLong(p[2].substring(2), 16);
                boolean init = p[3].equals("1");
                String src = p[4];
                if (init) {
                    byte[] bytes = Files.readAllBytes(base.resolve(src));
                    byte[] buf = new byte[(int) size];
                    System.arraycopy(bytes, 0, buf, 0, Math.min(bytes.length, (int) size));
                    mem.createInitializedBlock(name, addr,
                        new ByteArrayInputStream(buf), size, monitor, false);
                } else {
                    mem.createUninitializedBlock(name, addr, size, false);
                }
                println("block " + name + " @ " + addr + " size 0x" + Long.toHexString(size));
            }

            // --- set A5 register to its constant value across all code blocks ---
            long a5val = 0;
            for (String line : Files.readAllLines(meta)) {
                String[] p = line.split("\t");
                if (p[0].equals("A5")) a5val = Long.parseLong(p[1].substring(2), 16);
            }
            Register a5 = currentProgram.getRegister("A5");
            ProgramContext ctx = currentProgram.getProgramContext();
            BigInteger a5bi = BigInteger.valueOf(a5val);
            for (MemoryBlock blk : mem.getBlocks()) {
                if (blk.getName().startsWith("CODE_")) {
                    try { ctx.setValue(a5, blk.getStart(), blk.getEnd(), a5bi); }
                    catch (Exception e) { }
                }
            }
            println("A5 set to 0x" + Long.toHexString(a5val));

            // --- disassemble jump-table JMPs so cross-segment refs resolve ---
            long jtBase = 0; int jtCount = 0;
            for (String line : Files.readAllLines(meta)) {
                String[] p = line.split("\t");
                if (p[0].equals("JT_BASE")) jtBase = Long.parseLong(p[1].substring(2), 16);
                if (p[0].equals("JT_COUNT")) jtCount = Integer.parseInt(p[1]);
            }
            for (int i = 0; i < jtCount; i++) {
                Address jmp = toAddr(jtBase + i * 8L + 2);   // +2 = the JMP; callers target this
                try { new DisassembleCommand(jmp, null, false).applyTo(currentProgram, monitor); }
                catch (Exception e) { }
            }

            // --- create functions at all exported entry points ---
            int created = 0;
            List<Address> eps = new ArrayList<>();
            for (String line : Files.readAllLines(entries)) {
                line = line.trim();
                if (line.isEmpty()) continue;
                Address ep = toAddr(Long.parseLong(line, 16));
                eps.add(ep);
                try { new DisassembleCommand(ep, null, true).applyTo(currentProgram, monitor); }
                catch (Exception e) { }
                if (currentProgram.getFunctionManager().getFunctionAt(ep) == null) {
                    try { if (createFunction(ep, null) != null) created++; } catch (Exception e) { }
                }
            }
            println("entry functions created: " + created + " / " + eps.size());
        } finally {
            currentProgram.endTransaction(tx, true);
        }

        // --- analyze (discovers internal functions, resolves calls) ---
        analyzeAll(currentProgram);

        // --- apply symbol names (optional args[5] = symbols.tsv: addr<TAB>name<TAB>G|F) ---
        if (a.length > 5) {
            Path symPath = Paths.get(a[5]);
            if (Files.exists(symPath)) {
                int tx2 = currentProgram.startTransaction("apply symbols");
                int ng = 0, nf = 0;
                try {
                    for (String line : Files.readAllLines(symPath)) {
                        line = line.trim();
                        if (line.isEmpty() || line.startsWith("#")) continue;
                        String[] p = line.split("\t");
                        Address addr = toAddr(Long.parseLong(p[0].substring(2), 16));
                        String name = p[1];
                        String kind = p.length > 2 ? p[2] : "G";
                        try {
                            if (kind.equals("F")) {
                                Function f = currentProgram.getFunctionManager().getFunctionAt(addr);
                                if (f != null) { f.setName(name, SourceType.USER_DEFINED); nf++; }
                                else { createLabel(addr, name, true); nf++; }
                            } else {
                                createLabel(addr, name, true);
                                ng++;
                            }
                        } catch (Exception e) { }
                    }
                } finally { currentProgram.endTransaction(tx2, true); }
                println("symbols applied: globals=" + ng + " funcs=" + nf);
            }
        }

        // --- dump code/data map: ranges NOT covered by instructions = data ---
        try {
            Listing lst = currentProgram.getListing();
            PrintWriter cm = new PrintWriter(new FileWriter(base.resolve("recon/code_map.tsv").toString()));
            cm.println("segment\tdata_start\tdata_end");
            for (MemoryBlock blk : mem.getBlocks()) {
                if (!blk.getName().startsWith("CODE_")) continue;
                int seg = Integer.parseInt(blk.getName().substring(5));
                long base0 = blk.getStart().getOffset();
                int size = (int) blk.getSize();
                boolean[] code = new boolean[size];
                InstructionIterator it = lst.getInstructions(
                        new AddressSet(blk.getStart(), blk.getEnd()), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    int s = (int) (ins.getMinAddress().getOffset() - base0);
                    for (int k = 0; k < ins.getLength(); k++) {
                        int idx = s + k; if (idx >= 0 && idx < size) code[idx] = true;
                    }
                }
                int i = 0;
                while (i < size) {
                    if (!code[i]) {
                        int j = i; while (j < size && !code[j]) j++;
                        cm.println("CODE" + seg + "\t0x" + Integer.toHexString(i)
                                   + "\t0x" + Integer.toHexString(j));
                        i = j;
                    } else i++;
                }
            }
            cm.close();
            println("wrote recon/code_map.tsv");
        } catch (Exception e) { println("code_map dump failed: " + e); }

        // --- decompile every function ---
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(new FileWriter(outfile));
        out.println("// combined-image decompilation (A5 world + jump table)");
        int ok = 0, fail = 0, total = 0;
        for (Function func : fm.getFunctions(true)) {
            total++;
            try {
                DecompileResults r = decomp.decompileFunction(func, 120, monitor);
                if (r != null && r.decompileCompleted()) {
                    out.println("/* ---- " + func.getName() + " @ " + func.getEntryPoint() + " ---- */");
                    out.println(r.getDecompiledFunction().getC());
                    out.println();
                    ok++;
                } else { fail++; }
            } catch (Exception e) { fail++; }
        }
        out.close();
        println("COMBINED_FUNCS_TOTAL=" + total + " OK=" + ok + " FAIL=" + fail);
    }
}
