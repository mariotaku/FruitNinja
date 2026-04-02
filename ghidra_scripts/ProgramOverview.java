// ProgramOverview.java
//@author Claude
//@category Analysis
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;

public class ProgramOverview extends GhidraScript {
    public void run() throws Exception {
        Program prog = currentProgram;
        if (prog == null) { println("No program loaded."); return; }

        println("=== PROGRAM OVERVIEW ===");
        println("Name        : " + prog.getName());
        println("Location    : " + prog.getExecutablePath());
        println("Format      : " + prog.getExecutableFormat());
        println("Language    : " + prog.getLanguage().getLanguageID());
        println("Compiler    : " + prog.getCompilerSpec().getCompilerSpecID());
        println("Image Base  : " + prog.getImageBase());
        println();

        println("=== MEMORY MAP ===");
        for (MemoryBlock block : prog.getMemory().getBlocks()) {
            println(String.format("  %-30s  %s - %s  size=0x%x  %s%s%s%s",
                block.getName(),
                block.getStart(), block.getEnd(),
                block.getSize(),
                block.isRead()?"R":"-",
                block.isWrite()?"W":"-",
                block.isExecute()?"X":"-",
                block.isInitialized()?"I":"-"));
        }
        println();

        FunctionManager fm = prog.getFunctionManager();
        int totalFuncs = fm.getFunctionCount();
        int namedFuncs = 0;
        int thunkFuncs = 0;
        for (Function f : fm.getFunctions(true)) {
            if (!f.getName().startsWith("FUN_") && !f.getName().startsWith("thunk_")) namedFuncs++;
            if (f.isThunk()) thunkFuncs++;
        }
        println("=== FUNCTIONS ===");
        println("  Total     : " + totalFuncs);
        println("  Named     : " + namedFuncs);
        println("  Thunks    : " + thunkFuncs);
        println();

        println("=== IMPORTS ===");
        ExternalManager em = prog.getExternalManager();
        String[] libs = em.getExternalLibraryNames();
        println("  Libraries (" + libs.length + "):");
        for (int i = 0; i < libs.length; i++) {
            String lib = libs[i];
            int count = 0;
            ExternalLocationIterator it = em.getExternalLocations(lib);
            while (it.hasNext()) {
                it.next();
                count++;
            }
            println("    " + lib + " (" + count + " imports)");
        }
        println();

        println("=== ENTRY POINTS ===");
        AddressIterator entries = prog.getSymbolTable().getExternalEntryPointIterator();
        int epCount = 0;
        while (entries.hasNext()) {
            Address ep = entries.next();
            Symbol sym = prog.getSymbolTable().getPrimarySymbol(ep);
            println("  " + ep + " : " + (sym != null ? sym.getName() : "<unnamed>"));
            if (++epCount >= 10) { println("  ... (first 10 only)"); break; }
        }
        if (epCount == 0) println("  (none)");
        println();

        println("=== NAMED FUNCTIONS (first 50) ===");
        int shown = 0;
        for (Function f : fm.getFunctions(true)) {
            String name = f.getName();
            if (!name.startsWith("FUN_") && !name.startsWith("thunk_")) {
                println(String.format("  %s  %s  [%d params]",
                    f.getEntryPoint(), name, f.getParameterCount()));
                if (++shown >= 50) break;
            }
        }
        if (shown == 0) println("  (all auto-named)");
        println("=== END ===");
    }
}
