// FN09_PushEngineFindings.java
// Push newly discovered engine struct fields and calling conventions to Ghidra
// Based on analysis findings: struct sizing, vtable CC fixes, function renames

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.program.model.data.ParameterDefinitionImpl;

public class FN09_PushEngineFindings extends GhidraScript {

    private DataTypeManager dtm;
    private int successCount = 0;
    private int failCount = 0;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        int txId = currentProgram.startTransaction("FN09_PushEngineFindings");
        try {
            // === PART 1: Fix HUDControl vtable stub calling conventions ===
            println("=== Fixing HUDControl vtable stub calling conventions ===");
            fixVtableStubCC(0x00143f98, "Init", "void __thiscall Init(HUDControl * this)");
            fixVtableStubCC(0x00143f9c, "Release", "void __thiscall Release(HUDControl * this)");
            fixVtableStubCC(0x00143fa0, "Reset", "void __thiscall Reset(HUDControl * this)");
            fixVtableStubCC(0x0014492c, "BeginDraw", "void __thiscall BeginDraw(HUDControl * this, float dt)");
            fixVtableStubCC(0x00144930, "PreDraw", "void __thiscall PreDraw(HUDControl * this, float * matrix)");
            fixVtableStubCC(0x00143fa4, "Draw", "void __thiscall Draw(HUDControl * this, float * matrix)");
            fixVtableStubCC(0x00144934, "PreDrawOrder", "void __thiscall PreDrawOrder(HUDControl * this, float * matrix, int order)");
            fixVtableStubCC(0x00144940, "DrawOrder", "void __thiscall DrawOrder(HUDControl * this, float * matrix, int order)");
            fixVtableStubCC(0x00143fa8, "Update", "void __thiscall Update(HUDControl * this, float dt)");
            fixVtableStubCC(0x00143fac, "SetToMultiplayerState", "void __thiscall SetToMultiplayerState(HUDControl * this)");
            fixVtableStubCC(0x0014494c, "GetType", "int __thiscall GetType(HUDControl * this)");
            fixVtableStubCC(0x00144950, "Skip", "void __thiscall Skip(HUDControl * this)");
            fixVtableStubCC(0x00144954, "Save", "void __thiscall Save(HUDControl * this)");

            // === PART 2: Fix Geometry::Render prototype ===
            println("=== Fixing function prototypes ===");
            fixVtableStubCC(0x001a3e98, "Geometry::Render", "void __thiscall Render(Geometry * this)");

            // === PART 3: Create/update structs ===
            println("=== Creating/updating structs ===");
            createGLFuncParams();
            createPassBinding();
            createEffectProperty();
            createBakedString();

            println("=== Summary: " + successCount + " succeeded, " + failCount + " failed ===");
            currentProgram.endTransaction(txId, true);
        } catch (Exception e) {
            currentProgram.endTransaction(txId, false);
            throw e;
        }
    }

    private void fixVtableStubCC(long addr, String name, String prototype) {
        try {
            Address address = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addr);
            Function func = currentProgram.getFunctionManager().getFunctionAt(address);
            if (func == null) {
                println("  WARN: No function at 0x" + Long.toHexString(addr) + " (" + name + ")");
                failCount++;
                return;
            }
            // Parse prototype to set calling convention to __thiscall
            func.setCallingConvention("__thiscall");
            println("  OK: Set __thiscall on " + name + " at 0x" + Long.toHexString(addr));
            successCount++;
        } catch (Exception e) {
            println("  FAIL: " + name + " at 0x" + Long.toHexString(addr) + ": " + e.getMessage());
            failCount++;
        }
    }

    private StructureDataType getOrCreateStruct(String categoryPath, String name, int size) {
        // Try to find existing
        CategoryPath catPath = new CategoryPath(categoryPath);
        DataType existing = dtm.getDataType(catPath, name);
        if (existing != null && existing instanceof Structure) {
            Structure s = (Structure) existing;
            if (s.getLength() >= size) {
                println("  Struct " + name + " already exists with size " + s.getLength());
                return null; // already good
            }
            // Delete the old one so we can recreate
            dtm.remove(existing, monitor);
            println("  Removed old " + name + " (size " + s.getLength() + ") to recreate at size " + size);
        }
        StructureDataType sdt = new StructureDataType(catPath, name, size);
        return sdt;
    }

    private void createGLFuncParams() {
        try {
            // GLFuncParams: 0x14 = 20 bytes
            // Fields: m_Func (int/enum), m_Param0..m_Param3 (int each)
            StructureDataType sdt = getOrCreateStruct("/FruitNinja", "GLFuncParams", 0x14);
            if (sdt == null) return;
            sdt.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_Func", "GL function enum");
            sdt.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Param0", null);
            sdt.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_Param1", null);
            sdt.replaceAtOffset(0x0C, IntegerDataType.dataType, 4, "m_Param2", null);
            sdt.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "m_Param3", null);
            dtm.addDataType(sdt, DataTypeConflictHandler.REPLACE_HANDLER);
            println("  OK: Created GLFuncParams (20 bytes)");
            successCount++;
        } catch (Exception e) {
            println("  FAIL: GLFuncParams: " + e.getMessage());
            failCount++;
        }
    }

    private void createPassBinding() {
        try {
            // PassBinding: 0x68 = 104 bytes
            // 5 GLFuncParams blocks (0x14 each) at offsets 0, 0x14, 0x28, 0x3C, 0x50
            // m_pIndexStream at +0x64 (pointer)
            CategoryPath catPath = new CategoryPath("/FruitNinja");

            // Make sure GLFuncParams exists first
            DataType glFuncParams = dtm.getDataType(catPath, "GLFuncParams");
            if (glFuncParams == null) {
                println("  WARN: GLFuncParams not found, using placeholder for PassBinding");
                glFuncParams = new ArrayDataType(ByteDataType.dataType, 0x14, 1);
            }

            StructureDataType sdt = getOrCreateStruct("/FruitNinja", "PassBinding", 0x68);
            if (sdt == null) return;
            sdt.replaceAtOffset(0x00, glFuncParams, glFuncParams.getLength(), "m_Params0", "GLFuncParams block 0");
            sdt.replaceAtOffset(0x14, glFuncParams, glFuncParams.getLength(), "m_Params1", "GLFuncParams block 1");
            sdt.replaceAtOffset(0x28, glFuncParams, glFuncParams.getLength(), "m_Params2", "GLFuncParams block 2");
            sdt.replaceAtOffset(0x3C, glFuncParams, glFuncParams.getLength(), "m_Params3", "GLFuncParams block 3");
            sdt.replaceAtOffset(0x50, glFuncParams, glFuncParams.getLength(), "m_Params4", "GLFuncParams block 4");
            sdt.replaceAtOffset(0x64, PointerDataType.dataType, 4, "m_pIndexStream", "Index stream pointer");
            dtm.addDataType(sdt, DataTypeConflictHandler.REPLACE_HANDLER);
            println("  OK: Created PassBinding (104 bytes)");
            successCount++;
        } catch (Exception e) {
            println("  FAIL: PassBinding: " + e.getMessage());
            failCount++;
        }
    }

    private void createEffectProperty() {
        try {
            // EffectProperty: 0x14 = 20 bytes
            StructureDataType sdt = getOrCreateStruct("/FruitNinja", "EffectProperty", 0x14);
            if (sdt == null) return;
            sdt.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_NameHash", "String hash of property name");
            sdt.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Type", "Property type enum");
            sdt.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_Location", "GL uniform location");
            sdt.replaceAtOffset(0x0C, IntegerDataType.dataType, 4, "m_Count", "Element count");
            sdt.replaceAtOffset(0x10, PointerDataType.dataType, 4, "m_pData", "Pointer to value data");
            dtm.addDataType(sdt, DataTypeConflictHandler.REPLACE_HANDLER);
            println("  OK: Created EffectProperty (20 bytes)");
            successCount++;
        } catch (Exception e) {
            println("  FAIL: EffectProperty: " + e.getMessage());
            failCount++;
        }
    }

    private void createBakedString() {
        try {
            // BakedString: 0x1C = 28 bytes (may have vtable at 0, or may not)
            // Fields: m_pTextures(ptr), m_PageCount(int), m_pVertexData(ptr),
            //         m_pVertexCounts(ptr), m_Width(float), m_Height(float)
            // With 6 fields * 4 bytes = 24, plus possible padding = 28
            StructureDataType sdt = getOrCreateStruct("/FruitNinja", "BakedString", 0x1C);
            if (sdt == null) return;
            sdt.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pTextures", "Array of texture pointers");
            sdt.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_PageCount", "Number of texture pages");
            sdt.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pVertexData", "Vertex data per page");
            sdt.replaceAtOffset(0x0C, PointerDataType.dataType, 4, "m_pVertexCounts", "Vertex count per page");
            sdt.replaceAtOffset(0x10, new FloatDataType(), 4, "m_Width", "Baked string width");
            sdt.replaceAtOffset(0x14, new FloatDataType(), 4, "m_Height", "Baked string height");
            // 0x18-0x1B: padding or extra field
            sdt.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "m_Alignment", "Text alignment");
            dtm.addDataType(sdt, DataTypeConflictHandler.REPLACE_HANDLER);
            println("  OK: Created BakedString (28 bytes)");
            successCount++;
        } catch (Exception e) {
            println("  FAIL: BakedString: " + e.getMessage());
            failCount++;
        }
    }
}
