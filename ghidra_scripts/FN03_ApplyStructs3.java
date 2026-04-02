// ApplyStructs3.java — Mesh/Model/ResourceLoader structs
// Run AFTER ApplyStructs.java and ApplyStructs2.java
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN03_ApplyStructs3 extends GhidraScript {

    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");

        int txId = currentProgram.startTransaction("Apply mesh/model structs");
        try {
            createLegacyPSPVertexDecl();
            createVertexElement();
            createIVertexSource();
            createIIndexSource();
            createResourceLoader();
            createBadaTextureHeader();
            createPCMAudioHeader();
            createHBR0Header();
            println("Done! Mesh/model structs created under /FruitNinja category.");
        } finally {
            currentProgram.endTransaction(txId, true);
        }
    }

    private StructureDataType make(String name, int size) {
        return new StructureDataType(cat, name, size);
    }

    private void commit(StructureDataType s) {
        dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    private void createLegacyPSPVertexDecl() {
        // Unpacked from a uint32 bitfield in LoadVertexStreamPSP
        StructureDataType s = make("LegacyPSPVertexDecl", 0x38);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "texCoordFmt", "bits[1:0] 0=none,1=u8,2=u16,3=float");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "weightFmt", "bits[4:2]");
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "colorFmt", "bits[6:5] 0=none,7=8888");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "normalFmt", "bits[8:7] 0=none,1=s8,2=s16,3=float");
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "positionFmt", "bits[10:9]");
        s.replaceAtOffset(0x14, IntegerDataType.dataType, 4, "field_14", "bits[12:11]");
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "field_18_unused", null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType, 4, "morphBoneCount", "bits[15:13]");
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "field_20_unused", null);
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "weightCount", "bits[18:16]");
        s.replaceAtOffset(0x28, IntegerDataType.dataType, 4, "flag2D", "bit[19]");
        s.replaceAtOffset(0x2c, IntegerDataType.dataType, 4, "field_2c", "bits[21:20]");
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "field_30", "bits[23:22] color in GenerateElementListing");
        s.replaceAtOffset(0x34, IntegerDataType.dataType, 4, "field_34", "normal in GenerateElementListing");
        commit(s);
    }

    private void createVertexElement() {
        StructureDataType s = make("VertexElement", 0x14);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_SemanticName", "Immutable<string>");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_SemanticName2", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_ComponentCount", "2=pos, 3=normal/color, 4=weights");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "m_Offset", "byte offset within vertex");
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "m_DataType", "format enum from LookupTable");
        commit(s);
    }

    private void createIVertexSource() {
        StructureDataType s = make("IVertexSource", 0x38);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pVertexData", "raw vertex bytes");
        s.replaceAtOffset(0x20, ByteDataType.dataType, 1, "m_bOwnsData", null);
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "m_VertexCount", null);
        // +0x28: vector<VertexElement> (12 bytes)
        s.replaceAtOffset(0x34, IntegerDataType.dataType, 4, "m_Stride", "bytes per vertex");
        commit(s);
    }

    private void createIIndexSource() {
        StructureDataType s = make("IIndexSource", 0x20);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_PrimType", "0=pts,1=lines,2=tris,3=strip,5=fan");
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pIndexData", "raw index bytes");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "m_IndexCount", null);
        s.replaceAtOffset(0x10, ByteDataType.dataType, 1, "m_bOwnsData", null);
        s.replaceAtOffset(0x14, IntegerDataType.dataType, 4, "m_IndexSize", "1=u8, 2=u16");
        commit(s);
    }

    private void createResourceLoader() {
        StructureDataType s = make("ResourceLoader", 0x44);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_TypeId", null);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "m_BasePath", "AsciiString");
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_BasePath2", null);
        // +0x2c: vector<byte> rawData (12 bytes)
        // +0x38: vector<ResourceLoader> children (12 bytes)
        commit(s);
    }

    private void createBadaTextureHeader() {
        StructureDataType s = make("BadaTextureHeader", 12);
        s.replaceAtOffset(0x00, ByteDataType.dataType, 1, "widthLog2", "width = 1 << widthLog2");
        s.replaceAtOffset(0x01, ByteDataType.dataType, 1, "heightLog2", "height = 1 << heightLog2");
        s.replaceAtOffset(0x02, ByteDataType.dataType, 1, "format", "0x10=RGBA4444, 0x11=RGB565");
        s.replaceAtOffset(0x03, ByteDataType.dataType, 1, "padding", null);
        s.replaceAtOffset(0x04, UnsignedShortDataType.dataType, 2, "width", null);
        s.replaceAtOffset(0x06, UnsignedShortDataType.dataType, 2, "height", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "sentinel", "0xFFFFFFFF");
        commit(s);
    }

    private void createPCMAudioHeader() {
        StructureDataType s = make("PCMAudioHeader", 20);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "channels", "always 1 (mono)");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "sampleRate", "always 16000");
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "bitsPerSample", "always 16");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "numSamples", null);
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "reserved", "always 0");
        commit(s);
    }

    private void createHBR0Header() {
        StructureDataType s = make("HBR0Header", 12);
        s.replaceAtOffset(0x00, new ArrayDataType(CharDataType.dataType, 4, 1), 4, "magic", "HBR0");
        s.replaceAtOffset(0x04, UnsignedShortDataType.dataType, 2, "type", "0=data,1=node,2=container");
        s.replaceAtOffset(0x06, UnsignedShortDataType.dataType, 2, "padding", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "dataSize", null);
        commit(s);
    }
}
