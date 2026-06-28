// FN_FixStdContainerSizes.java
// Fix size of std::vector/list/map/basic_string + SmartPtr placeholders in Ghidra DB.
//
// Ground-truth evidence (binary-verified via field-offset diffs):
//   - std::list<T>      = 8  bytes (Sourcery 2010q1 pre-C++11; see PowerUpManager 0x00117d20)
//     and Mesh.h offsets m_ActiveScreenEffects@0x50 -> m_PurchasablePowers@0x58 = 8B
//   - std::map<K,V>     = 24 bytes (PowerUpManager 0x18-byte gaps between consecutive maps)
//   - std::vector<T>    = 12 bytes (Mesh m_BoneBindings@0x34 -> m_Geometries@0x40 = 12B,
//     m_Geometries@0x40 -> m_OwnGroup@0x4C = 12B)
//   - std::basic_string = 4  bytes (Sourcery 2010q1 single pointer COW rep)
//   - Mortar::SmartPtr  = 4  bytes (Mesh m_OwnGroup@0x4C -> m_GroupsByName@0x50 = 4B)
//   - _List_base<T>     = 8  bytes (already proven on _List_base<HUDControl*>)
//   - _Vector_base<T>   = 12 bytes
//   - _Rb_tree<...>     = 24 bytes
//   - new_allocator<T>  = 1  byte (empty allocator -- LEAVE AT 1)
//   - less<T>           = 1  byte (empty comparator -- LEAVE AT 1)
//   - _Select1st<T>     = 1  byte (empty selector -- LEAVE AT 1)
//
// Strategy: ONLY resize, do not change internal layout (per task spec --
// "size correctness is what matters for field-offset propagation"). Fill with
// a single byte[N] field named _opaque.
//
// Args: "DRY" -> dry run, print plan. Anything else (or no args) -> apply.
//
// @category FruitNinja
// @runtime Java
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import java.util.*;

public class FN_FixStdContainerSizes extends GhidraScript {
    boolean dryRun = false;
    int planned = 0;
    int applied = 0;
    int skipped = 0;
    List<String> log = new ArrayList<String>();

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args != null && args.length > 0 && args[0].equalsIgnoreCase("DRY")) {
            dryRun = true;
        }
        println("FN_FixStdContainerSizes -- " + (dryRun ? "DRY RUN" : "APPLY"));
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        Iterator<DataType> it = dtm.getAllDataTypes();
        List<DataType> targets = new ArrayList<DataType>();
        while (it.hasNext()) targets.add(it.next());

        for (DataType dt : targets) {
            if (!(dt instanceof Structure)) continue;
            Structure s = (Structure) dt;
            String name = s.getName();
            int curSize = s.isZeroLength() ? 0 : s.getLength();
            int newSize = -1;
            String reason = null;

            // Match on prefix to catch all template instantiations.
            if (name.startsWith("basic_string<")) {
                newSize = 4; reason = "std::basic_string (Sourcery COW single-ptr)";
            } else if (name.startsWith("vector<")) {
                newSize = 12; reason = "std::vector (3 ptrs: begin/end/cap)";
            } else if (name.startsWith("_Vector_base<")) {
                newSize = 12; reason = "_Vector_base (3 ptrs)";
            } else if (name.startsWith("list<")) {
                newSize = 8; reason = "std::list (sentinel prev/next)";
            } else if (name.startsWith("_List_base<")) {
                newSize = 8; reason = "_List_base (sentinel prev/next)";
            } else if (name.startsWith("map<") || name.startsWith("multimap<")) {
                newSize = 24; reason = "std::map (_Rb_tree header+count)";
            } else if (name.startsWith("set<") || name.startsWith("multiset<")) {
                newSize = 24; reason = "std::set (_Rb_tree header+count)";
            } else if (name.startsWith("_Rb_tree<")) {
                newSize = 24; reason = "_Rb_tree (header node + count)";
            } else if (name.startsWith("SmartPtr<")) {
                // Only top-level Mortar::SmartPtr<T> (path-based filter).
                String path = s.getCategoryPath().getPath();
                if (!path.startsWith("/Demangler/Mortar")) continue;
                newSize = 4; reason = "Mortar::SmartPtr<T> (single intrusive ptr)";
            } else {
                continue;
            }

            // Skip if already correct size.
            if (curSize == newSize) { skipped++; continue; }

            // Skip unparameterized stub types (no '<' in name).
            if (!name.contains("<")) {
                log.add("[SKIP-stub] " + name + " (unparameterized stub)");
                skipped++;
                continue;
            }

            planned++;
            log.add("[FIX] " + curSize + "B -> " + newSize + "B  " + name + "  (" + reason + ")");

            if (!dryRun) {
                try {
                    Structure newStruct = new StructureDataType(s.getCategoryPath(), s.getName(), 0, dtm);
                    DataType bArr = new ArrayDataType(new ByteDataType(), newSize, 1, dtm);
                    newStruct.add(bArr, "_opaque", "binary-verified size; internal layout opaque");
                    s.replaceWith(newStruct);
                    applied++;
                } catch (Exception e) {
                    log.add("[ERROR] " + name + ": " + e.getMessage());
                }
            }
        }

        for (String line : log) println(line);
        println("---- SUMMARY ----");
        println("Planned:  " + planned);
        println("Applied:  " + applied);
        println("Skipped:  " + skipped);
        println("DryRun:   " + dryRun);
    }
}
