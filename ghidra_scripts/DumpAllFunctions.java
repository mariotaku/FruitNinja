// DumpAllFunctions.java - Dump all function names with namespaces
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;
import java.io.*;

public class DumpAllFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        
        // Write to a file for easy retrieval
        String outputPath = System.getProperty("user.home") + File.separator + "ghidra_classes_output.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outputPath));
        
        // Collect all classes
        TreeMap<String, List<String>> classes = new TreeMap<>();
        TreeMap<String, Long> minAddr = new TreeMap<>();
        TreeMap<String, Long> maxAddr = new TreeMap<>();
        Set<String> vtableClasses = new HashSet<>();
        int total = 0, classified = 0;

        FunctionIterator fi = fm.getFunctions(true);
        while (fi.hasNext()) {
            Function f = fi.next();
            total++;
            Namespace ns = f.getParentNamespace();
            if (ns != null && !ns.isGlobal()) {
                String cn = ns.getName(true);
                long a = f.getEntryPoint().getOffset();
                classes.computeIfAbsent(cn, k -> new ArrayList<>()).add(
                    String.format("%08x:%s", a, f.getName()));
                minAddr.merge(cn, a, Math::min);
                maxAddr.merge(cn, a, Math::max);
                classified++;
            }
        }

        // Find vtables
        SymbolIterator si = st.getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String sn = s.getName();
            if (sn.contains("vtable") || sn.contains("vftable") || sn.startsWith("_ZTV") ||
                sn.contains("typeinfo") || sn.startsWith("_ZTI") || sn.startsWith("_ZTS")) {
                Namespace ns = s.getParentNamespace();
                String cn = (ns != null && !ns.isGlobal()) ? ns.getName(true) : sn;
                vtableClasses.add(cn);
                classes.computeIfAbsent(cn, k -> new ArrayList<>()).add(
                    String.format("%08x:[symbol]%s", s.getAddress().getOffset(), sn));
                long a = s.getAddress().getOffset();
                minAddr.merge(cn, a, Math::min);
                maxAddr.merge(cn, a, Math::max);
            }
        }

        pw.printf("SUMMARY|total_funcs=%d|classified=%d|classes=%d|vtable_classes=%d\n",
            total, classified, classes.size(), vtableClasses.size());

        for (Map.Entry<String, List<String>> e : classes.entrySet()) {
            String cn = e.getKey();
            List<String> methods = e.getValue();
            Collections.sort(methods);
            boolean vt = vtableClasses.contains(cn);
            pw.printf("CLASS|%s|vtable=%s|methods=%d|range=%08x-%08x\n",
                cn, vt, methods.size(),
                minAddr.get(cn), maxAddr.get(cn));
            for (String m : methods) {
                pw.printf("  METHOD|%s\n", m);
            }
        }
        pw.printf("END_CLASSES\n");
        pw.close();
        
        // Also print summary to console
        println("Output written to: " + outputPath);
        println(String.format("Total functions: %d, Classified: %d, Classes: %d, Vtable classes: %d",
            total, classified, classes.size(), vtableClasses.size()));
        
        // Print a compact version to console too
        for (Map.Entry<String, List<String>> e : classes.entrySet()) {
            String cn = e.getKey();
            List<String> methods = e.getValue();
            boolean vt = vtableClasses.contains(cn);
            printf("CLASS|%s|vtable=%s|methods=%d|range=%08x-%08x|", 
                cn, vt, methods.size(), minAddr.get(cn), maxAddr.get(cn));
            Collections.sort(methods);
            printf("%s\n", String.join(";", methods));
        }
    }
}
