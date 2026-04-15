// FindSliceKeyframes.java
// Find the function that initialises the SliceEffect keyframe Vec3 table
// at 0x001ec038+0x68 = 0x001ec0a0 and read 6 Vec3s from it.
//@category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class FindSliceKeyframes extends GhidraScript {
    @Override
    public void run() throws Exception {
        // The slice data object lives in BSS at 0x001ec038.
        // The keyframe table is at +0x68 = 0x001ec0a0.
        // Since BSS is zero-init, the table must be filled at runtime.
        // Walk references TO the region 0x001ec038..0x001ec0e8 to find the writer.

        AddressFactory af = currentProgram.getAddressFactory();
        Memory mem = currentProgram.getMemory();
        ReferenceManager rm = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        // Print 6 Vec3s (72 bytes) from 0x001ec0a0 raw — these are runtime values so 0 in binary
        Address tableBase = af.getAddress("001ec0a0");
        println("=== Raw bytes at 0x001ec0a0 (keyframe table, will be 0 in static binary) ===");
        byte[] raw = new byte[72];
        mem.getBytes(tableBase, raw);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 72; i++) sb.append(String.format("%02x ", raw[i] & 0xFF));
        println(sb.toString());

        // Now find all xrefs INTO the slice-data BSS region 0x001ec038..0x001ec0f0
        Address bssStart = af.getAddress("001ec038");
        Address bssEnd   = af.getAddress("001ec0f0");
        println("\n=== References into slice-data BSS (0x001ec038..0x001ec0f0) ===");
        for (Address a = bssStart; a.compareTo(bssEnd) <= 0; a = a.add(4)) {
            for (ghidra.program.model.symbol.Reference ref : rm.getReferencesTo(a)) {
                Address from = ref.getFromAddress();
                Function fn = fm.getFunctionContaining(from);
                println(String.format("  0x%s -> 0x%s  in %s",
                    from, a,
                    fn != null ? fn.getName() + " @ " + fn.getEntryPoint() : "<no function>"));
            }
        }

        // Also look for the static ctor list — search for functions that call _Vector3 constructors
        // near the GOT offset DAT_00169c44 = 0x000452d4
        // GOT base from DrawSlices: 0x169ad8 + DAT_00169c40(=0x082658) = 0x1ec130
        // *(0x1ec130) = 0x001ec038  (slice data ptr)
        println("\n=== References to 0x001ec130 (g_sliceData ptr) ===");
        Address ptrAddr = af.getAddress("001ec130");
        for (ghidra.program.model.symbol.Reference ref : rm.getReferencesTo(ptrAddr)) {
            Address from = ref.getFromAddress();
            Function fn = fm.getFunctionContaining(from);
            println(String.format("  0x%s  in %s",
                from,
                fn != null ? fn.getName() + " @ " + fn.getEntryPoint() : "<no function>"));
        }
    }
}
