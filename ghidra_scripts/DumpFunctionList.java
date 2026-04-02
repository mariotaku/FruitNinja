// DumpFunctionList.java - Write all functions to file
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpFunctionList extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outputPath = System.getProperty("user.home") + "/ghidra_function_list.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outputPath));
        
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator fi = fm.getFunctions(true);
        int count = 0;
        while (fi.hasNext()) {
            Function f = fi.next();
            Namespace ns = f.getParentNamespace();
            String nsName = (ns != null && !ns.isGlobal()) ? ns.getName(true) : "<global>";
            pw.printf("%s|%s|0x%08x|%s\n", nsName, f.getName(), 
                f.getEntryPoint().getOffset(), f.isThunk() ? "thunk" : "func");
            count++;
        }
        
        // Also dump symbols
        pw.println("---SYMBOLS---");
        SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
        int symCount = 0;
        while (si.hasNext()) {
            Symbol s = si.next();
            if (s.getSymbolType() == SymbolType.LABEL || s.getSymbolType() == SymbolType.CLASS || 
                s.getSymbolType() == SymbolType.NAMESPACE) {
                Namespace ns = s.getParentNamespace();
                String nsName = (ns != null && !ns.isGlobal()) ? ns.getName(true) : "<global>";
                pw.printf("SYM|%s|%s|0x%08x|%s\n", nsName, s.getName(), 
                    s.getAddress().getOffset(), s.getSymbolType());
                symCount++;
            }
        }
        
        pw.close();
        println(String.format("Wrote %d functions and %d symbols to %s", count, symCount, outputPath));
    }
}
