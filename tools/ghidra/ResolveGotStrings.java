// Reference template: resolve GOT-relative string pointer tables.
//
// This script is the canonical implementation for the pattern used in
// Fruit::LoadInfo (v1.6.1 ~0x00180xxx): GOT-relative DAT slots whose
// contents are pointers to strings. The hardcoded addresses below are
// specific to Fruit::LoadInfo attribute names and serve as a working
// example and test case.
//
// USAGE AS A TEMPLATE
// For a different function: replace `gotBaseLit` (the LDR literal that
// loads the GOT base offset from .rodata) and `r4Offset` (the addend
// applied to the loaded base to form r4), then update the `slots` table
// with the relevant DAT_ addresses and labels.
//
// Mechanism (ARM32 GOT-relative):
//   r4 = *gotBaseLit + r4Offset   -- forms the absolute GOT pointer base
//   For each attribute slot addr S:
//     rel   = *(S)                 -- 4-byte signed offset stored at S
//     ptr1  = r4 + rel             -- points to a 4-byte slot in the GOT
//     strPtr = *(ptr1)             -- the actual char* string address
//     str   = read_cstring(strPtr)
//
// Run via GhidraMCP run_ghidra_script with the program open:
//   run_script_inline("<source of this file>")
//   -- or --
//   run_ghidra_script("ResolveGotStrings")  // if loaded into Ghidra script dir
//
// @category FruitNinja
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class ResolveGotStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();

        // --- Fruit::LoadInfo GOT base (v1.6.1) ---
        // LDR r3, [pc, #N] loads a relative offset from 0x0017a25c;
        // r4 = r3 + 0x179888 forms the absolute base pointer.
        Address gotBaseLit = toAddr(0x0017a25c);
        long gotBaseRel    = mem.getInt(gotBaseLit) & 0xFFFFFFFFL;
        long r4            = (gotBaseRel + 0x179888L) & 0xFFFFFFFFL;
        println(String.format("GOT base r4 = 0x%08x", r4));

        // GOT slot literals to resolve: {address, label}
        // Each address holds a 4-byte signed offset from r4 to a slot
        // that itself holds the char* string pointer.
        long[][] slots = {
            {0x0017a264L, "DAT_0017a264 (HUD shadow tex name?)"},
            {0x0017a268L, "DAT_0017a268"},
            {0x0017a26cL, "DAT_0017a26c (xml/fruitList.xml)"},
            {0x0017a270L, "DAT_0017a270 (root tag)"},
            {0x0017a274L, "DAT_0017a274 (settings tag?)"},
            {0x0017a278L, "DAT_0017a278 (colour attr?)"},
            {0x0017a27cL, "DAT_0017a27c (default colour bytes)"},
            {0x0017a280L, "DAT_0017a280"},
            {0x0017a288L, "DAT_0017a288"},
            {0x0017a290L, "DAT_0017a290 (score)"},
            {0x0017a298L, "DAT_0017a298 (chance/weight)"},
            {0x0017a2a0L, "DAT_0017a2a0"},
            {0x0017a2a8L, "DAT_0017a2a8"},
            {0x0017a2b0L, "DAT_0017a2b0 (scale)"},
            {0x0017a2b8L, "DAT_0017a2b8"},
            {0x0017a2c0L, "DAT_0017a2c0"},
            {0x0017a2c4L, "DAT_0017a2c4 (bomb tag)"},
            {0x0017a2c8L, "DAT_0017a2c8 (size)"},
            {0x0017a2d0L, "DAT_0017a2d0 (collision)"},
            {0x0017a2d4L, "DAT_0017a2d4 (FruitInfo tag)"},
            {0x0017a2dcL, "DAT_0017a2dc (name)"},
            {0x0017a2e0L, "DAT_0017a2e0 (%s_trail)"},
            {0x0017a2e4L, "DAT_0017a2e4 (%s_sliced)"},
            {0x0017a2e8L, "DAT_0017a2e8 (%s_total)"},
            {0x0017a2ecL, "DAT_0017a2ec (%s_point_total)"},
            {0x0017a2f0L, "DAT_0017a2f0 (%s_drops)"},
            {0x0017a2f4L, "DAT_0017a2f4 (hud_%s.tex)"},
            {0x0017a2f8L, "DAT_0017a2f8 (zen_%s.tex)"},
            {0x0017a2fcL, "DAT_0017a2fc (plural)"},
            {0x0017a300L, "DAT_0017a300 (%ss)"},
            {0x0017a304L, "DAT_0017a304 (pluralEnglish)"},
            {0x0017a308L, "DAT_0017a308 (singular)"},
            {0x0017a30cL, "DAT_0017a30c (singularEnglish)"},
            {0x0017a310L, "DAT_0017a310 (factTexture)"},
            {0x0017a314L, "DAT_0017a314 (modelName)"},
            {0x0017a318L, "DAT_0017a318 (factColour)"},
            {0x0017a31cL, "DAT_0017a31c (hitInfluence?)"},
            {0x0017a320L, "DAT_0017a320 (coinsMin)"},
            {0x0017a324L, "DAT_0017a324 (coinsMax / next)"},
            {0x0017a328L, "DAT_0017a328 (next2)"},
            {0x0017a32cL, "DAT_0017a32c (next3)"},
            {0x0017a330L, "DAT_0017a330 (hasSplatSeeds?)"},
            {0x0017a334L, "DAT_0017a334 (splats?)"},
            {0x0017a338L, "DAT_0017a338 (true/yes?)"},
            {0x0017a33cL, "DAT_0017a33c (onside?)"},
            {0x0017a340L, "DAT_0017a340 (onlySprinkle?)"},
            {0x0017a344L, "DAT_0017a344 (fact tag)"},
            {0x0017a348L, "DAT_0017a348 (sound tag)"},
            {0x0017a34cL, "DAT_0017a34c (sfx %c%s)"},
            {0x0017a350L, "DAT_0017a350 (power tag)"},
        };

        for (long[] slot : slots) {
            long addr  = slot[0];
            String label = "" + slot[1];
            Address a  = toAddr(addr);
            int rel    = mem.getInt(a);
            long ptr1  = (r4 + (rel & 0xFFFFFFFFL)) & 0xFFFFFFFFL;
            String s1;
            try {
                int contents = mem.getInt(toAddr(ptr1));
                long strAddr = contents & 0xFFFFFFFFL;
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < 64; i++) {
                    byte b = mem.getByte(toAddr(strAddr + i));
                    if (b == 0) break;
                    if (b < 0x20 || b > 0x7e) {
                        sb.append(String.format("\\x%02x", b & 0xff));
                    } else {
                        sb.append((char) b);
                    }
                }
                s1 = String.format("got=0x%08x ptr=0x%08x str=\"%s\"", ptr1, strAddr, sb.toString());
            } catch (Exception e) {
                s1 = String.format("got=0x%08x [unreadable: %s]", ptr1, e.getMessage());
            }
            println(String.format("%-50s -> %s", label, s1));
        }
    }
}
