// FN09_MatrixStructs.java — Create MatrixStack and MatrixManager structs
// @category FruitNinja
// @author Claude

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN09_MatrixStructs extends GhidraScript {
    @Override
    public void run() throws Exception {
        var dtm = currentProgram.getDataTypeManager();
        int tx = dtm.startTransaction("Create MatrixStack and MatrixManager structs");
        try {
            CategoryPath catPath = new CategoryPath("/FruitNinja");
            dtm.createCategory(catPath);

            // Find _Matrix44<float>
            StructureDataType mat44 = null;
            for (DataType dt : dtm.getAllDataTypes()) {
                if (dt.getName().equals("_Matrix44<float>") && dt instanceof StructureDataType) {
                    mat44 = (StructureDataType) dt;
                    break;
                }
            }
            if (mat44 == null) {
                println("ERROR: _Matrix44<float> not found");
                dtm.endTransaction(tx, false);
                return;
            }
            println("Found _Matrix44<float>: " + mat44.getLength() + " bytes");

            // Delete old versions if they exist
            DataType oldMS = dtm.getDataType(catPath, "MatrixStack");
            if (oldMS != null) dtm.remove(oldMS, monitor);
            DataType oldMM = dtm.getDataType(catPath, "MatrixManager");
            if (oldMM != null) dtm.remove(oldMM, monitor);

            // Also check root category
            oldMS = dtm.getDataType(CategoryPath.ROOT, "MatrixStack");
            if (oldMS != null) dtm.remove(oldMS, monitor);
            oldMM = dtm.getDataType(CategoryPath.ROOT, "MatrixManager");
            if (oldMM != null) dtm.remove(oldMM, monitor);

            // === MatrixStack (0x848 = 2120 bytes) ===
            StructureDataType ms = new StructureDataType(catPath, "MatrixStack", 0x848);
            ms.clearComponent(0);  // clear auto-fill

            // +0x800: m_Current (_Matrix44<float>, 64 bytes)
            ms.replaceAtOffset(0x800, mat44, mat44.getLength(), "m_Current", "Current (top) matrix");
            // +0x840: m_Depth
            ms.replaceAtOffset(0x840, IntegerDataType.dataType, 4, "m_Depth", "Stack depth (0 = reset)");
            // +0x844: m_Version
            ms.replaceAtOffset(0x844, IntegerDataType.dataType, 4, "m_Version", "Modification counter");

            DataType msType = dtm.addDataType(ms, DataTypeConflictHandler.REPLACE_HANDLER);
            println("MatrixStack created: " + msType.getLength() + " bytes (expected 2120)");

            // === MatrixManager (0x2134 = 8500 bytes) ===
            StructureDataType mm = new StructureDataType(catPath, "MatrixManager", 0x2134);
            mm.clearComponent(0);

            // +0x00: fns (vtable pointer)
            mm.replaceAtOffset(0x00, PointerDataType.dataType, 4, "fns", "vtable pointer");
            // +0x04: m_Projection
            mm.replaceAtOffset(0x04, msType, msType.getLength(), "m_Projection", "Stack 0 - Projection (GL_PROJECTION)");
            // +0x84C: m_View
            mm.replaceAtOffset(0x84C, msType, msType.getLength(), "m_View", "Stack 1 - View (GL_MODELVIEW base)");
            // +0x1094: m_World
            mm.replaceAtOffset(0x1094, msType, msType.getLength(), "m_World", "Stack 2 - World (GL_MODELVIEW local)");
            // +0x18DC: m_Texture
            mm.replaceAtOffset(0x18DC, msType, msType.getLength(), "m_Texture", "Stack 3 - Texture (GL_TEXTURE)");
            // +0x2124: m_ViewVersion
            mm.replaceAtOffset(0x2124, IntegerDataType.dataType, 4, "m_ViewVersion", "Cached version of m_View");
            // +0x2128: m_ViewVersionUploaded
            mm.replaceAtOffset(0x2128, IntegerDataType.dataType, 4, "m_ViewVersionUploaded", "Last uploaded m_View version");
            // +0x212C: m_WorldVersionUploaded
            mm.replaceAtOffset(0x212C, IntegerDataType.dataType, 4, "m_WorldVersionUploaded", "Last uploaded m_World version");
            // +0x2130: m_TextureVersionUploaded
            mm.replaceAtOffset(0x2130, IntegerDataType.dataType, 4, "m_TextureVersionUploaded", "Last uploaded m_Texture version");

            DataType mmType = dtm.addDataType(mm, DataTypeConflictHandler.REPLACE_HANDLER);
            println("MatrixManager created: " + mmType.getLength() + " bytes (expected 8500)");

            dtm.endTransaction(tx, true);
            println("Done! Both structs created in /FruitNinja category.");
        } catch (Exception e) {
            dtm.endTransaction(tx, false);
            println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
