// Find all EffectProperty and SharedEffectProperties functions
//@author RE
//@category RE
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.util.*;

public class FindEffectPropFuncs extends GhidraScript {
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionIterator iter = fm.getFunctions(true);
        List<String> results = new ArrayList<>();
        while (iter.hasNext()) {
            Function f = iter.next();
            String name = f.getName(true);
            if (name.contains("EffectProperty") || name.contains("SharedEffectProp")) {
                results.add(f.getEntryPoint() + " | " + name);
            }
        }
        Collections.sort(results);
        for (String s : results) println(s);
    }
}
