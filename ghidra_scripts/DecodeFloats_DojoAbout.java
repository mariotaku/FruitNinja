//@category FruitNinja
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;

public class DecodeFloats_DojoAbout extends GhidraScript {
    @Override
    public void run() throws Exception {
        long[] addrs = {
            // DojoScreen Update
            0x00138684L, 0x00138688L, 0x0013868cL, 0x00138690L, 0x00138694L,
            0x001389d0L, 0x001389d4L, 0x001389d8L, 0x001389dcL, 0x001389e0L,
            // DojoScreen Draw
            0x001383c0L, 0x001383c4L, 0x001383c8L, 0x001383ccL, 0x001383d0L,
            // AboutScreen ctor init alpha
            0x0012ed88L,
            // AboutScreen Update
            0x0012f2f4L, 0x0012f2f8L, 0x0012f2fcL, 0x0012f300L, 0x0012f304L,
            0x0012f328L,
            // AboutScreen Draw panel 1
            0x0012f690L, 0x0012f694L, 0x0012f698L, 0x0012f69cL,
            0x0012f6a0L, 0x0012f6a4L, 0x0012f6a8L, 0x0012f6acL, 0x0012f6b0L,
            0x0012f6b4L, 0x0012f6b8L,
            // AboutScreen Draw panel 2 (credits tex)
            0x0012f8d8L, 0x0012f8dcL, 0x0012f8e0L, 0x0012f8e4L,
            0x0012f8e8L, 0x0012f8ecL, 0x0012f8f0L,
        };

        Memory mem = currentProgram.getMemory();
        AddressFactory af = currentProgram.getAddressFactory();
        for (long a : addrs) {
            Address addr = af.getDefaultAddressSpace().getAddress(a);
            byte[] b = new byte[4];
            mem.getBytes(addr, b);
            int bits = ((b[3] & 0xFF) << 24) | ((b[2] & 0xFF) << 16) | ((b[1] & 0xFF) << 8) | (b[0] & 0xFF);
            float f = Float.intBitsToFloat(bits);
            println(String.format("0x%08x = %f", a, f));
        }
    }
}
