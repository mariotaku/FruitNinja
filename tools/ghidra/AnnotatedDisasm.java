// Print per-instruction annotated disassembly for the function at the current
// cursor position.  Each branch/call instruction is annotated with the target
// function name where resolvable (via ReferenceManager + getFunctionAt).
//
// Run via GhidraMCP run_ghidra_script with currentAddress set to any
// instruction inside the target function.
//
// @category FruitNinja
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;

public class AnnotatedDisasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        if (currentAddress == null) {
            printerr("No current address. Position cursor at a function.");
            return;
        }
        Function func = getFunctionContaining(currentAddress);
        if (func == null) {
            printerr("No function at " + currentAddress);
            return;
        }
        AddressSetView body = func.getBody();
        Listing listing = currentProgram.getListing();
        ReferenceManager refMgr = currentProgram.getReferenceManager();

        CodeUnitIterator it = listing.getCodeUnits(body, true);
        while (it.hasNext() && !monitor.isCancelled()) {
            CodeUnit cu = it.next();
            Address addr = cu.getAddress();

            Reference[] refs = refMgr.getFlowReferencesFrom(addr);
            String comment = "";
            if (refs.length > 0) {
                Address target = refs[0].getToAddress();
                if (target.getOffset() > 0x100000) {
                    Function targetFunc = getFunctionAt(target);
                    if (targetFunc != null) {
                        comment = "  ; -> " + targetFunc.getName();
                    }
                }
            }

            printf("%08x: %s%s\n", addr.getOffset(), cu.toString(), comment);
        }
    }
}
