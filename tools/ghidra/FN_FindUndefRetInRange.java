// Find non-thunk non-external functions with undefined return type in an
// address range.  Skips std / __gnu_cxx / __cxxabiv1 namespaces, ctors
// (name == short namespace name), and dtors (name starts with ~).
//
// Script args (optional, both hex without 0x prefix):
//   args[0] = lo address  (default: 0x000000)
//   args[1] = hi address  (default: 0xFFFFFFF -- scan all)
//
// Run via GhidraMCP run_ghidra_script, e.g.:
//   run_ghidra_script("FN_FindUndefRetInRange", args=["158000", "180000"])
//   run_ghidra_script("FN_FindUndefRetInRange", args=["180000", "1b9220"])
//
// Output format (one function per line after ---LIST---):
//   <ep_hex>|<return_type>|<func_name>|<parent_namespace>
//
// @category FruitNinja
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class FN_FindUndefRetInRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long loVal = (args != null && args.length > 0) ? Long.parseLong(args[0], 16) : 0x000000L;
        long hiVal = (args != null && args.length > 1) ? Long.parseLong(args[1], 16) : 0xFFFFFFFL;

        Address lo = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(loVal);
        Address hi = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(hiVal);

        FunctionManager fm = currentProgram.getFunctionManager();

        int total    = 0;
        int undefRet = 0;
        StringBuilder sb = new StringBuilder();

        for (Function f : fm.getFunctions(true)) {
            Address ep = f.getEntryPoint();
            if (ep.compareTo(lo) < 0) continue;
            if (ep.compareTo(hi) >= 0) continue;
            if (f.isThunk()) continue;
            if (f.isExternal()) continue;

            String parent = f.getParentNamespace().getName(true);
            if (parent.startsWith("std") ||
                parent.contains("__gnu_cxx") ||
                parent.contains("__cxxabiv1") ||
                parent.equals("<EXTERNAL>")) continue;

            String name    = f.getName();
            String localNs = f.getParentNamespace().getName();

            // Skip ctors (name matches short namespace name) and dtors (~Name)
            if (name.startsWith("~")) continue;
            if (!localNs.equals("Global") && name.equals(localNs)) continue;

            total++;
            String rt = f.getReturnType().getName();
            if (rt.equals("undefined") || rt.equals("undefined4") ||
                rt.equals("undefined1") || rt.equals("undefined2") ||
                rt.equals("undefined8")) {
                undefRet++;
                sb.append(ep.toString()).append("|")
                  .append(rt).append("|")
                  .append(name).append("|")
                  .append(parent).append("\n");
            }
        }

        println("LO=0x" + Long.toHexString(loVal) + "  HI=0x" + Long.toHexString(hiVal));
        println("TOTAL_IN_RANGE=" + total);
        println("UNDEF_RET=" + undefRet);
        println("---LIST---");
        print(sb.toString());
    }
}
