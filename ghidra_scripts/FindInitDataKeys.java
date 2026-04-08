// FindInitDataKeys.java
// Find the stat key strings used in InitialiseData (0x0010b66c)
// key1 = GOT_base + literal_at_0x0010b7e4
// key2 = GOT_base + literal_at_0x0010b7e8
// GOT_base verified: r4 + 0x7990 = 0x001F3AC0 (g_GameData GOT slot)
//@author port-RE
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class FindInitDataKeys extends GhidraScript {
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        AddressFactory af = currentProgram.getAddressFactory();

        // Read literal pool values
        int lit1 = mem.getInt(af.getAddress("0x0010b7e4"));  // signed
        int lit2 = mem.getInt(af.getAddress("0x0010b7e8"));  // signed
        println("lit1 = 0x" + Integer.toHexString(lit1) + " (" + lit1 + ")");
        println("lit2 = 0x" + Integer.toHexString(lit2) + " (" + lit2 + ")");

        // Verify GOT base via g_GameData GOT slot
        // GOT_base + 0x7990 = GOT slot addr 0x001F3AC0
        // → GOT_base = 0x001F3AC0 - 0x7990 = 0x001EBB30?  or 0x001EC130?
        // Verify: read GOT slot to see g_GameData addr
        int gotSlot7990_base1 = 0x001EC130;
        int gotSlot7990_base2 = 0x001EBB30;
        println("If GOT=0x001EC130: g_GameData slot at 0x" + Integer.toHexString(gotSlot7990_base1 + 0x7990));
        println("If GOT=0x001EBB30: g_GameData slot at 0x" + Integer.toHexString(gotSlot7990_base2 + 0x7990));

        // Read actual g_GameData GOT slot (we know g_GameData addr from init)
        // Try both: check what's in mem there
        for (int base : new int[]{0x001EC130, 0x001EBB30}) {
            int slotAddr = base + 0x7990;
            try {
                int slotVal = mem.getInt(af.getAddress(Integer.toHexString(slotAddr)));
                println("GOT_base=0x" + Integer.toHexString(base) +
                        " → slot[0x7990] @ 0x" + Integer.toHexString(slotAddr) +
                        " = 0x" + Integer.toHexString(slotVal));
            } catch (Exception e) {
                println("GOT_base=0x" + Integer.toHexString(base) + ": slot read failed: " + e.getMessage());
            }
        }

        // Try both bases for key1
        for (int base : new int[]{0x001EC130, 0x001EBB30}) {
            int key1Addr = base + lit1;
            println("\n--- GOT_base=0x" + Integer.toHexString(base) + " ---");
            println("key1 @ 0x" + Integer.toHexString(key1Addr));
            try {
                byte[] buf = new byte[16];
                mem.getBytes(af.getAddress(Integer.toHexString(key1Addr)), buf);
                StringBuilder sb = new StringBuilder("  bytes: ");
                for (byte b : buf) sb.append(String.format("%02x(%c) ", b, (b>=32&&b<127)?(char)b:'.'));
                println(sb.toString());
            } catch (Exception e) {
                println("  read failed: " + e.getMessage());
            }

            int key2Addr = base + lit2;
            println("key2 @ 0x" + Integer.toHexString(key2Addr));
            try {
                byte[] buf = new byte[16];
                mem.getBytes(af.getAddress(Integer.toHexString(key2Addr)), buf);
                StringBuilder sb = new StringBuilder("  bytes: ");
                for (byte b : buf) sb.append(String.format("%02x(%c) ", b, (b>=32&&b<127)?(char)b:'.'));
                println(sb.toString());
            } catch (Exception e) {
                println("  read failed: " + e.getMessage());
            }
        }
    }
}
