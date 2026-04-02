// QuickOverview.java
//@author Claude
//@category Analysis
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class QuickOverview extends GhidraScript {
    public void run() throws Exception {
        Program p = currentProgram;
        println("=== " + p.getName() + " ===");
        println("Format  : " + p.getExecutableFormat());
        println("Language: " + p.getLanguage().getLanguageID());
        println("Compiler: " + p.getCompilerSpec().getCompilerSpecID());
        println("Base    : " + p.getImageBase());
        println();

        println("--- Memory Segments ---");
        for (MemoryBlock b : p.getMemory().getBlocks())
            println(String.format("  %-20s %s-%s  %s%s%s",
                b.getName(), b.getStart(), b.getEnd(),
                b.isRead()?"R":"-", b.isWrite()?"W":"-", b.isExecute()?"X":"-"));
        println();

        println("--- Functions ---");
        int total = p.getFunctionManager().getFunctionCount();
        int named = 0;
        StringBuilder sb = new StringBuilder();
        for (Function f : p.getFunctionManager().getFunctions(true)) {
            String n = f.getName();
            if (!n.startsWith("FUN_") && !n.startsWith("thunk_")) {
                named++;
                if (named <= 50)
                    sb.append("  ").append(f.getEntryPoint()).append("  ").append(n).append("\n");
            }
        }
        println("Total: " + total + "  Named: " + named);
        if (named > 0) println(sb.toString());

        println("--- Imports ---");
        ExternalManager em = p.getExternalManager();
        String[] libs = em.getExternalLibraryNames();
        for (int i = 0; i < libs.length; i++) {
            String lib = libs[i];
            int cnt = 0;
            ExternalLocationIterator it = em.getExternalLocations(lib);
            while (it.hasNext()) {
                it.next();
                cnt++;
            }
            println("  " + lib + " (" + cnt + ")");
        }
    }
}
