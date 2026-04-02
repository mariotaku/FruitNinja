// ReplaceTypes.java — Replaces Demangler auto-generated structs with /FruitNinja/ typed versions
// Run AFTER ApplyStructs.java has created the /FruitNinja types
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import java.util.*;

public class ReplaceTypes extends GhidraScript {

    @Override
    public void run() throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CategoryPath fnCat = new CategoryPath("/FruitNinja");

        // Map of demangler name -> our FruitNinja name
        // Keys: names as they appear in Demangler category (or root)
        // Values: names as created by ApplyStructs in /FruitNinja/
        String[][] mappings = {
            {"Fruit", "Fruit"},
            {"Bomb", "Bomb"},
            {"SlashEntity", "SlashEntity"},
            {"SlashEntityGhost", null},  // skip if we don't have it
            {"Game", "Game"},
            {"FruitCamera", "FruitCamera"},
            {"MortarCamera", "MortarCamera"},
            {"WaveManager", "WaveManager"},
            {"HUD", "HUD"},
            {"HUDControl", "HUDControl"},
            {"HUDControl3d", null},
            {"MissControl", "MissControl"},
            {"Col", null},
            {"ColLine", "ColLine"},
            {"ColSphere", "ColSphere"},
            {"FRUIT_INFO", "FRUIT_INFO"},
            {"SPAWNER_INFO", "SPAWNER_INFO"},
            {"ImpactSound", "ImpactSound"},
            {"FRUIT_POWER", "FRUIT_POWER"},
            {"FRUIT_POWERS", "FRUIT_POWERS"},
            {"FruitModelInfo", "FruitModelInfo"},
            {"MAMAudioThread", "MAMAudioThread"},
            {"Entity", "MortarEntity"},
            {"Mortar::Entity", "MortarEntity"},
            {"MortarEntity", "MortarEntity"},
            {"Colour", "Colour"},
            {"PowerUpManager", null},
            {"BonusManager", null},
            {"FruitSaveData", null},
            {"ItemManager", null},
            {"SplatEntity", null},
            {"PSPParticleManager", null},
            {"PSPParticleEmitter", null},
            {"GameSound", null},
            {"FruitNinja", null},
            {"GlesForm", null},
            {"WAVE_INFO", "WAVE_INFO"},
            {"DEFAULT_WAVE_INFO", "DEFAULT_WAVE_INFO"},
            {"COIN_CHANCEINATOR", "COIN_CHANCEINATOR"},
            {"WaveQueItem", null},
            {"WaveQue", null},
        };

        int txId = currentProgram.startTransaction("Replace Demangler types with FruitNinja types");
        int replaced = 0;
        int skipped = 0;

        try {
            for (String[] mapping : mappings) {
                String demanglerName = mapping[0];
                String fnName = mapping[1];

                if (fnName == null) {
                    // No replacement available yet, skip
                    continue;
                }

                DataType fnType = dtm.getDataType(fnCat, fnName);
                if (fnType == null) {
                    println("WARNING: /FruitNinja/" + fnName + " not found. Run ApplyStructs first.");
                    continue;
                }

                // Search all categories for the demangler type
                List<DataType> found = new ArrayList<>();
                dtm.findDataTypes(demanglerName, found);

                for (DataType dt : found) {
                    // Skip our own /FruitNinja/ types
                    if (dt.getCategoryPath().equals(fnCat)) continue;
                    // Skip built-in types
                    if (dt.getCategoryPath().toString().startsWith("/BuiltIn")) continue;

                    // Only replace struct/typedef types, not function defs
                    if (dt instanceof Structure || dt instanceof TypeDef) {
                        String oldPath = dt.getPathName();
                        try {
                            dtm.replaceDataType(dt, fnType, true);
                            println("REPLACED: " + oldPath + " -> /FruitNinja/" + fnName);
                            replaced++;
                        } catch (Exception e) {
                            println("FAILED to replace " + oldPath + ": " + e.getMessage());
                            skipped++;
                        }
                    }
                }
            }

            println("\nDone! Replaced " + replaced + " types, skipped " + skipped + ".");
        } finally {
            currentProgram.endTransaction(txId, true);
        }
    }
}
