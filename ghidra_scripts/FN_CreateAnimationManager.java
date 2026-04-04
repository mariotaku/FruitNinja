// FN_CreateAnimationManager.java — Create AnimationManager struct (20 bytes, List<Animation*>)
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN_CreateAnimationManager extends GhidraScript {
    @Override
    public void run() throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CategoryPath cat = new CategoryPath("/FruitNinja");
        int txId = currentProgram.startTransaction("Create AnimationManager struct");
        try {
            StructureDataType s = new StructureDataType(cat, "AnimationManager", 20);
            s.replaceAtOffset(0, PointerDataType.dataType, 4, "m_items", "Pointer to Animation* array (FreeList)");
            s.replaceAtOffset(4, UnsignedIntegerDataType.dataType, 4, "m_count", "Number of items in list");
            s.replaceAtOffset(8, UnsignedIntegerDataType.dataType, 4, "m_capacity", "Capacity of list");
            s.replaceAtOffset(12, UnsignedIntegerDataType.dataType, 4, "field_0xc", "Unknown field");
            s.replaceAtOffset(16, ShortDataType.dataType, 2, "m_flags", "List flags (ushort)");
            s.replaceAtOffset(18, ShortDataType.dataType, 2, "field_0x12", "Unknown (ushort)");
            dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
            println("AnimationManager struct created: 20 bytes, 6 fields");
        } finally {
            currentProgram.endTransaction(txId, true);
        }
    }
}
