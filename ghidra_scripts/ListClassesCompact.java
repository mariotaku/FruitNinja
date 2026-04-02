// ListClassesCompact.java - Extract all C++ classes compactly
// @category Analysis

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

public class ListClassesCompact extends GhidraScript {
    @Override
    public void run() throws Exception {
        TreeMap<String, List<String>> classes = new TreeMap<>();
        TreeMap<String, Long> minAddr = new TreeMap<>();
        TreeMap<String, Long> maxAddr = new TreeMap<>();
        Set<String> vtableClasses = new HashSet<>();

        FunctionManager fm = currentProgram.getFunctionManager();
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

        SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String sn = s.getName();
            if (sn.contains("vtable") || sn.contains("vftable") || sn.startsWith("_ZTV")) {
                Namespace ns = s.getParentNamespace();
                String cn = (ns != null && !ns.isGlobal()) ? ns.getName(true) : sn;
                vtableClasses.add(cn);
                classes.computeIfAbsent(cn, k -> new ArrayList<>()).add(
                    String.format("%08x:[vtable]%s", s.getAddress().getOffset(), sn));
                long a = s.getAddress().getOffset();
                minAddr.merge(cn, a, Math::min);
                maxAddr.merge(cn, a, Math::max);
            }
        }

        printf("SUMMARY|total_funcs=%d|classified=%d|classes=%d|vtable_classes=%d\n",
            total, classified, classes.size(), vtableClasses.size());

        for (Map.Entry<String, List<String>> e : classes.entrySet()) {
            String cn = e.getKey();
            List<String> methods = e.getValue();
            Collections.sort(methods);
            boolean vt = vtableClasses.contains(cn);
            printf("CLASS|%s|vtable=%s|methods=%d|range=%08x-%08x|%s\n",
                cn, vt, methods.size(),
                minAddr.get(cn), maxAddr.get(cn),
                String.join(";", methods));
        }
        printf("END_CLASSES\n");
    }
}
