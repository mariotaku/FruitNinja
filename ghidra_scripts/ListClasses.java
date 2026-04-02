// ListClasses.java - Extract all C++ classes from FruitNinja.exe (Bada OS)
// @category Analysis
// @description Lists all C++ classes, their methods, vtables, and inheritance
// 
// INSTRUCTIONS: Run this script from Ghidra's Script Manager.
// Output is printed to the Ghidra console AND written to ~/ghidra_classes_output.txt

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.util.*;
import java.io.*;

public class ListClasses extends GhidraScript {

    @Override
    public void run() throws Exception {
        // Map: className -> list of method info strings
        TreeMap<String, List<String>> classMethodMap = new TreeMap<>();
        // Map: className -> min address
        TreeMap<String, Long> classMinAddr = new TreeMap<>();
        // Map: className -> max address
        TreeMap<String, Long> classMaxAddr = new TreeMap<>();
        // Set of vtable classes
        Set<String> vtableClasses = new HashSet<>();

        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();

        int totalFunctions = 0;
        int classifiedFunctions = 0;

        // Pass 1: Iterate all functions and extract class::method patterns
        FunctionIterator funcIter = fm.getFunctions(true);
        while (funcIter.hasNext()) {
            Function func = funcIter.next();
            totalFunctions++;
            Namespace ns = func.getParentNamespace();
            if (ns != null && !ns.isGlobal()) {
                String className = ns.getName(true);
                long addr = func.getEntryPoint().getOffset();
                String methodInfo = String.format("0x%08x %s%s", addr, func.getName(),
                    func.isThunk() ? " [thunk]" : "");

                classMethodMap.computeIfAbsent(className, k -> new ArrayList<>()).add(methodInfo);
                classMinAddr.merge(className, addr, Math::min);
                classMaxAddr.merge(className, addr, Math::max);
                classifiedFunctions++;
            }
        }

        // Pass 2: Look for vtable/typeinfo symbols
        SymbolIterator symIter = st.getAllSymbols(true);
        while (symIter.hasNext()) {
            Symbol sym = symIter.next();
            String symName = sym.getName();
            if (symName.contains("vtable") || symName.contains("vftable") || 
                symName.startsWith("_ZTV") || symName.contains("typeinfo") ||
                symName.startsWith("_ZTI") || symName.startsWith("_ZTS")) {
                Namespace ns = sym.getParentNamespace();
                String className = (ns != null && !ns.isGlobal()) ? ns.getName(true) : symName;
                vtableClasses.add(className);
                long addr = sym.getAddress().getOffset();
                classMethodMap.computeIfAbsent(className, k -> new ArrayList<>()).add(
                    String.format("0x%08x [symbol] %s", addr, symName));
                classMinAddr.merge(className, addr, Math::min);
                classMaxAddr.merge(className, addr, Math::max);
            }
        }

        // Output
        String outputPath = System.getProperty("user.home") + File.separator + "ghidra_classes_output.txt";
        PrintWriter pw = new PrintWriter(new FileWriter(outputPath));

        String header = String.format(
            "=== C++ CLASS ANALYSIS REPORT ===\n" +
            "Binary: %s\n" +
            "Total functions: %d\n" +
            "Functions in classes/namespaces: %d\n" +
            "Total classes/namespaces found: %d\n" +
            "Classes with vtables: %d\n" +
            "================================\n",
            currentProgram.getName(), totalFunctions, classifiedFunctions,
            classMethodMap.size(), vtableClasses.size());
        
        println(header);
        pw.print(header);

        for (Map.Entry<String, List<String>> entry : classMethodMap.entrySet()) {
            String className = entry.getKey();
            List<String> methods = entry.getValue();
            Collections.sort(methods);

            String namespace = "";
            final String shortName;
            int lastSep = className.lastIndexOf("::");
            if (lastSep > 0) {
                namespace = className.substring(0, lastSep);
                shortName = className.substring(lastSep + 2);
            } else {
                shortName = className;
            }

            boolean hasVtable = vtableClasses.contains(className);
            boolean hasCtor = methods.stream().anyMatch(m -> m.contains(" " + shortName) && !m.contains("~"));
            boolean hasDtor = methods.stream().anyMatch(m -> m.contains("~" + shortName));

            StringBuilder sb = new StringBuilder();
            sb.append(String.format("\nCLASS: %s\n", className));
            if (!namespace.isEmpty()) sb.append(String.format("  Namespace: %s\n", namespace));
            sb.append(String.format("  Has vtable: %s\n", hasVtable));
            sb.append(String.format("  Has constructor: %s\n", hasCtor));
            sb.append(String.format("  Has destructor: %s\n", hasDtor));
            sb.append(String.format("  Method count: %d\n", methods.size()));
            sb.append(String.format("  Address range: 0x%08x - 0x%08x\n",
                classMinAddr.get(className), classMaxAddr.get(className)));
            sb.append("  Methods:\n");
            for (String m : methods) {
                sb.append(String.format("    %s\n", m));
            }

            String block = sb.toString();
            println(block);
            pw.print(block);
        }

        pw.println("\n=== END OF CLASS ANALYSIS ===");
        pw.close();
        println("\nOutput written to: " + outputPath);
    }
}
