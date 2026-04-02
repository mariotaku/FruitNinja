// ApplyPrototypes.java — Applies recovered struct types to function signatures
// Run AFTER ApplyStructs.java has created the /FruitNinja types
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.util.ArrayList;
import java.util.List;

public class ApplyPrototypes extends GhidraScript {

    private DataTypeManager dtm;
    private CategoryPath cat;
    private int count = 0;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");

        // Fruit methods
        apply("0x00177680", "Fruit", "void",  new String[]{"float"}, new String[]{"dt"});
        apply("0x00176708", "Fruit", "void",  new String[]{"void *","long","void *"}, new String[]{"p1","fruitType","scale"});
        apply("0x001780b0", "Fruit", "int",   new String[]{"void *","ulong","ulong","void *"}, new String[]{"slash","p2","p3","bladeVel"});
        apply("0x00175a64", "Fruit", "void",  new String[]{"float"}, new String[]{"delay"});
        apply("0x001764dc", "Fruit", "void *", new String[]{}, new String[]{});
        apply("0x00176abc", "Fruit", "void",  new String[]{"int"}, new String[]{"removeFromList"});
        apply("0x00176d58", "Fruit", "void",  new String[]{}, new String[]{});
        apply("0x001791f4", "Fruit", "void",  new String[]{}, new String[]{});
        apply("0x0017621c", "Fruit", "void",  new String[]{"uint","float"}, new String[]{"type","scale"});

        // Bomb methods
        apply("0x001729fc", "Bomb", "void",  new String[]{"float"}, new String[]{"dt"});
        apply("0x00172504", "Bomb", "void",  new String[]{"void *","long","void *"}, new String[]{"p1","type","p3"});
        apply("0x0017280c", "Bomb", "int",   new String[]{"void *","ulong","ulong","void *"}, new String[]{"slash","p2","p3","bladeVel"});
        apply("0x00170f68", "Bomb", "void",  new String[]{"float"}, new String[]{"delay"});
        apply("0x001716b0", "Bomb", "void *", new String[]{}, new String[]{});

        // SlashEntity methods
        apply("0x0017d664", "SlashEntity", "void", new String[]{"float"}, new String[]{"dt"});
        apply("0x0017c65c", "SlashEntity", "void", new String[]{"void *","long","void *"}, new String[]{"p1","p2","p3"});
        apply("0x0017b92c", "SlashEntity", "void", new String[]{"float"}, new String[]{"dt"});
        apply("0x0017b570", "SlashEntity", "int",  new String[]{"void *"}, new String[]{"entity"});
        apply("0x0017d2e4", "SlashEntity", "void", new String[]{"void *"}, new String[]{"inputEvent"});
        apply("0x0017e424", "SlashEntity", "void", new String[]{}, new String[]{});
        apply("0x0017e504", "SlashEntity", "void", new String[]{}, new String[]{});
        apply("0x0017b82c", "SlashEntity", "void", new String[]{}, new String[]{});

        // FruitCamera methods
        apply("0x00180de0", "FruitCamera", "void *", new String[]{}, new String[]{});
        apply("0x00180c8c", "FruitCamera", "float", new String[]{"float"}, new String[]{"dt"});
        apply("0x00180ea0", "FruitCamera", "void",  new String[]{"float"}, new String[]{"dt"});
        apply("0x00180d10", "FruitCamera", "void",  new String[]{"void *","float","float"}, new String[]{"impact","p2","p3"});
        apply("0x001810ac", "FruitCamera", "void",  new String[]{"int","int"}, new String[]{"p1","p2"});

        // WaveManager methods
        apply("0x001225a0", "WaveManager", "void", new String[]{"long","long","void *","int"}, new String[]{"count","fruitType","spawner","playerIdx"});
        apply("0x00121fa8", "WaveManager", "void", new String[]{"long","long","void *","int"}, new String[]{"count","type","spawner","playerIdx"});
        apply("0x001219c4", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x001219e4", "WaveManager", "int",  new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x00121834", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x00122e94", "WaveManager", "void", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x00123510", "WaveManager", "void", new String[]{"float","int"}, new String[]{"amount","playerIdx"});
        apply("0x001259d8", "WaveManager", "void", new String[]{"float"}, new String[]{"dt"});
        apply("0x00125340", "WaveManager", "void", new String[]{"void *","int","int"}, new String[]{"waveInfo","playerIdx","p3"});
        apply("0x00125390", "WaveManager", "void", new String[]{"float","int","int"}, new String[]{"dt","playerIdx","p3"});
        apply("0x00124f10", "WaveManager", "void *", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x001218dc", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x00121f90", "WaveManager", "void", new String[]{}, new String[]{});
        apply("0x0012872c", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x0012871c", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x0012870c", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});
        apply("0x001286fc", "WaveManager", "float", new String[]{"int"}, new String[]{"playerIdx"});

        // MissControl methods
        apply("0x001515a4", "MissControl", "void", new String[]{"void *","int","int"}, new String[]{"pos","comboCount","entityType"});
        apply("0x00150fa4", "MissControl", "void", new String[]{}, new String[]{});
        apply("0x00151a60", "MissControl", "void", new String[]{"float"}, new String[]{"dt"});
        apply("0x00151f60", "MissControl", "void", new String[]{"float"}, new String[]{"dt"});

        // HUD methods
        apply("0x00144d20", "HUD", "void", new String[]{"float"}, new String[]{"dt"});
        apply("0x00144a90", "HUD", "void", new String[]{"int"}, new String[]{"layerMask"});
        apply("0x00144b28", "HUD", "void", new String[]{"float"}, new String[]{"dt"});

        println("Done! Applied struct types to " + count + " function prototypes.");
    }

    private void apply(String addrStr, String structName, String retType, String[] paramTypes, String[] paramNames) {
        try {
            long addrLong = Long.parseLong(addrStr.replace("0x", ""), 16);
            Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(addrLong);
            Function func = currentProgram.getFunctionManager().getFunctionAt(addr);
            if (func == null) {
                println("WARNING: No function at " + addrStr);
                return;
            }

            DataType structType = dtm.getDataType(cat, structName);
            if (structType == null) {
                println("WARNING: Struct '" + structName + "' not found. Run ApplyStructs first.");
                return;
            }

            // Build FunctionDefinitionDataType
            FunctionDefinitionDataType fdef = new FunctionDefinitionDataType(func.getName());
            fdef.setReturnType(resolveType(retType));

            // Build parameter list: this + user params
            List<ParameterDefinition> params = new ArrayList<>();
            params.add(new ParameterDefinitionImpl("this", new PointerDataType(structType), null));
            for (int i = 0; i < paramTypes.length; i++) {
                params.add(new ParameterDefinitionImpl(paramNames[i], resolveType(paramTypes[i]), null));
            }
            fdef.setArguments(params.toArray(new ParameterDefinition[0]));

            // Apply via command
            ApplyFunctionSignatureCmd cmd = new ApplyFunctionSignatureCmd(
                addr, fdef, SourceType.USER_DEFINED);
            cmd.applyTo(currentProgram);

            count++;
        } catch (Exception e) {
            println("ERROR at " + addrStr + " (" + structName + "): " + e.getMessage());
        }
    }

    private DataType resolveType(String name) {
        switch (name) {
            case "void":   return VoidDataType.dataType;
            case "void *": return PointerDataType.dataType;
            case "int":    return IntegerDataType.dataType;
            case "uint":   return UnsignedIntegerDataType.dataType;
            case "long":   return LongDataType.dataType;
            case "ulong":  return UnsignedLongDataType.dataType;
            case "float":  return FloatDataType.dataType;
            case "char":   return CharDataType.dataType;
            case "byte":   return ByteDataType.dataType;
            default:
                DataType dt = dtm.getDataType(cat, name);
                if (dt != null) return dt;
                return VoidDataType.dataType;
        }
    }
}
