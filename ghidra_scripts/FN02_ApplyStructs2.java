// ApplyStructs2.java — Additional structs from resource analysis + remaining gaps
// Run AFTER ApplyStructs.java
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN02_ApplyStructs2 extends GhidraScript {

    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");

        int txId = currentProgram.startTransaction("Apply FruitNinja structs batch 2");
        try {
            createPSPParticleEmitter();
            createPSPParticleManager();
            createPSPEmitterTemplate();
            createPSPParticle();
            createBombFlash();
            createBombBlast();
            createSplatEntity();
            createPowerUp();
            createPowerUpManager();
            createGameSound();
            createGlesForm();
            createFruitNinjaApp();
            createTouchEvnt();
            createScreenEffect();
            createPurchaseInfo();
            println("Done! Additional structs created under /FruitNinja category.");
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

    // ===================== PARTICLE SYSTEM =====================

    private void createPSPParticleEmitter() {
        StructureDataType s = make("PSPParticleEmitter", 0x4c);
        s.replaceAtOffset(0x00, FloatDataType.dataType, 4, "m_Timer", null);
        s.replaceAtOffset(0x04, UnsignedShortDataType.dataType, 2, "m_ParticleHead", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "m_Pos_x", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_Pos_y", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "m_Pos_z", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "m_Vel_x", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "m_Vel_y", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "m_Vel_z", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "m_TimeScale", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_field24", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_ScaleX", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_ScaleY", null);
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "m_field30", null);
        s.replaceAtOffset(0x34, FloatDataType.dataType, 4, "m_field34", null);
        s.replaceAtOffset(0x38, ByteDataType.dataType, 1, "m_field38", null);
        s.replaceAtOffset(0x3c, PointerDataType.dataType, 4, "m_Template", null);
        s.replaceAtOffset(0x40, PointerDataType.dataType, 4, "m_Next", null);
        s.replaceAtOffset(0x44, PointerDataType.dataType, 4, "m_pRefPtr", null);
        s.replaceAtOffset(0x48, ByteDataType.dataType, 1, "m_bUpdateWhenPaused", null);
        commit(s);
    }

    private void createPSPParticleManager() {
        StructureDataType s = make("PSPParticleManager", 0x30);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pParticleArray", null);
        s.replaceAtOffset(0x04, UnsignedShortDataType.dataType, 2, "m_field04", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_ActiveCount", null);
        s.replaceAtOffset(0x0c, PointerDataType.dataType, 4, "m_ActiveList", null);
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "m_TemplateCount", null);
        s.replaceAtOffset(0x14, PointerDataType.dataType, 4, "m_pTemplates", null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "m_TemplateCount2", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pTemplateData", null);
        commit(s);
    }

    private void createPSPEmitterTemplate() {
        // Variable-size, but fixed header
        StructureDataType s = make("PSPEmitterTemplate", 0x4c);
        s.replaceAtOffset(0x40, IntegerDataType.dataType, 4, "m_Hash", null);
        s.replaceAtOffset(0x44, FloatDataType.dataType, 4, "m_MaxLifetime", null);
        s.replaceAtOffset(0x4b, ByteDataType.dataType, 1, "m_NumSets", null);
        commit(s);
    }

    private void createPSPParticle() {
        StructureDataType s = make("PSPParticle", 0xa4);
        s.replaceAtOffset(0x00, FloatDataType.dataType, 4, "m_Pos_x", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Pos_y", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "m_Pos_z", null);
        s.replaceAtOffset(0xa0, IntegerDataType.dataType, 4, "m_field44", null);
        commit(s);
    }

    // ===================== BOMB EFFECTS =====================

    private void createBombFlash() {
        StructureDataType s = make("BombFlash", 0x44);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Timer", null);
        s.replaceAtOffset(0x0b, ByteDataType.dataType, 1, "m_MaxAlpha", null);
        s.replaceAtOffset(0x0f, ByteDataType.dataType, 1, "m_CurrentAlpha", null);
        s.replaceAtOffset(0x34, FloatDataType.dataType, 4, "m_Scale_x", null);
        s.replaceAtOffset(0x38, FloatDataType.dataType, 4, "m_Scale_y", null);
        s.replaceAtOffset(0x3c, FloatDataType.dataType, 4, "m_Scale_z", null);
        s.replaceAtOffset(0x40, ByteDataType.dataType, 1, "m_bActive", null);
        commit(s);
    }

    private void createBombBlast() {
        StructureDataType s = make("BombBlast", 0x70);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "flags", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_BlastRadius", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_Scale", null);
        s.replaceAtOffset(0x3c, FloatDataType.dataType, 4, "m_PosA_x", null);
        s.replaceAtOffset(0x40, FloatDataType.dataType, 4, "m_PosA_y", null);
        s.replaceAtOffset(0x44, FloatDataType.dataType, 4, "m_PosA_z", null);
        s.replaceAtOffset(0x48, FloatDataType.dataType, 4, "m_PosB_x", null);
        s.replaceAtOffset(0x4c, FloatDataType.dataType, 4, "m_PosB_y", null);
        s.replaceAtOffset(0x50, FloatDataType.dataType, 4, "m_PosB_z", null);
        s.replaceAtOffset(0x54, FloatDataType.dataType, 4, "m_Vel1_x", null);
        s.replaceAtOffset(0x58, FloatDataType.dataType, 4, "m_Vel1_y", null);
        s.replaceAtOffset(0x5c, FloatDataType.dataType, 4, "m_Vel1_z", null);
        s.replaceAtOffset(0x60, FloatDataType.dataType, 4, "m_Vel2_x", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_Vel2_y", null);
        s.replaceAtOffset(0x68, FloatDataType.dataType, 4, "m_Vel2_z", null);
        s.replaceAtOffset(0x6c, FloatDataType.dataType, 4, "m_Lifetime", null);
        commit(s);
    }

    // ===================== SPLAT =====================

    private void createSplatEntity() {
        StructureDataType s = make("SplatEntity", 0x78);
        s.replaceAtOffset(0x14, IntegerDataType.dataType, 4, "m_FruitType", null);
        s.replaceAtOffset(0x18, ByteDataType.dataType, 1, "m_field18", null);
        s.replaceAtOffset(0x38, FloatDataType.dataType, 4, "m_Pos_x", null);
        s.replaceAtOffset(0x3c, FloatDataType.dataType, 4, "m_Pos_y", null);
        s.replaceAtOffset(0x40, FloatDataType.dataType, 4, "m_Pos_z", null);
        s.replaceAtOffset(0x5c, FloatDataType.dataType, 4, "m_Vel_x", null);
        s.replaceAtOffset(0x60, FloatDataType.dataType, 4, "m_Vel_y", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_Vel_z", null);
        s.replaceAtOffset(0x70, IntegerDataType.dataType, 4, "m_SplatType", null);
        s.replaceAtOffset(0x75, ByteDataType.dataType, 1, "m_bActive", null);
        commit(s);
    }

    // ===================== POWER-UP =====================

    private void createPowerUp() {
        StructureDataType s = make("PowerUp", 0xb8);
        // +0x04: list<GameModifier*> (12 bytes)
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "m_ModList_prev", null);
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_ModList_next", null);
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "m_NameHash", null);
        // +0x10: char[64] m_Name
        s.replaceAtOffset(0x10, new ArrayDataType(CharDataType.dataType, 0x40, 1), 0x40, "m_Name", null);
        // +0x50: char[64] m_DisplayName
        s.replaceAtOffset(0x50, new ArrayDataType(CharDataType.dataType, 0x40, 1), 0x40, "m_DisplayName", null);
        s.replaceAtOffset(0x90, ByteDataType.dataType, 1, "m_bIsPurchasable", null);
        s.replaceAtOffset(0x91, ByteDataType.dataType, 1, "m_bIsSpecial", null);
        s.replaceAtOffset(0x94, PointerDataType.dataType, 4, "m_pPurchaseInfo", null);
        s.replaceAtOffset(0xa0, FloatDataType.dataType, 4, "m_TotalTime", null);
        s.replaceAtOffset(0xa4, IntegerDataType.dataType, 4, "m_Colour", null);
        s.replaceAtOffset(0xac, PointerDataType.dataType, 4, "m_Texture1", null);
        s.replaceAtOffset(0xb0, PointerDataType.dataType, 4, "m_Texture2", null);
        s.replaceAtOffset(0xb4, PointerDataType.dataType, 4, "m_pScreenEffect", null);
        commit(s);
    }

    private void createPowerUpManager() {
        StructureDataType s = make("PowerUpManager", 0x90);
        // +0x00: map<ulong,PowerUp*> (24 bytes)
        // +0x18: list<PowerUp*> (12 bytes)
        s.replaceAtOffset(0x18, PointerDataType.dataType, 4, "m_ActiveList_prev", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_ActiveList_next", null);
        // +0x20: map<ulong,PowerUp*> (24 bytes)
        s.replaceAtOffset(0x60, IntegerDataType.dataType, 4, "m_field60", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_DtMod", null);
        s.replaceAtOffset(0x68, FloatDataType.dataType, 4, "m_field68", null);
        s.replaceAtOffset(0x6c, FloatDataType.dataType, 4, "m_field6c", null);
        s.replaceAtOffset(0x70, FloatDataType.dataType, 4, "m_field70", null);
        s.replaceAtOffset(0x78, IntegerDataType.dataType, 4, "m_ScoreGainMult", null);
        s.replaceAtOffset(0x7c, IntegerDataType.dataType, 4, "m_ScoreGainFactor", null);
        s.replaceAtOffset(0x88, FloatDataType.dataType, 4, "m_field88", null);
        commit(s);
    }

    // ===================== SOUND =====================

    private void createGameSound() {
        StructureDataType s = make("GameSound", 0x708);
        s.replaceAtOffset(0x00, FloatDataType.dataType, 4, "m_MasterVolume", null);
        // 32 sound slots × 0x38 bytes each starting at +0x08
        for (int i = 0; i < 4; i++) { // just label first 4 slots
            int off = 0x08 + i * 0x38;
            s.replaceAtOffset(off, PointerDataType.dataType, 4, "m_Slot" + i + "_pSound", null);
            s.replaceAtOffset(off + 0x04, IntegerDataType.dataType, 4, "m_Slot" + i + "_Hash", null);
            s.replaceAtOffset(off + 0x14, FloatDataType.dataType, 4, "m_Slot" + i + "_Volume", null);
            s.replaceAtOffset(off + 0x18, FloatDataType.dataType, 4, "m_Slot" + i + "_Pitch", null);
        }
        commit(s);
    }

    // ===================== APP / FORM =====================

    private void createGlesForm() {
        StructureDataType s = make("GlesForm", 0x1f8);
        // Bada Form base = 0x1c8 bytes
        s.replaceAtOffset(0x1c8, PointerDataType.dataType, 4, "touchListener", null);
        s.replaceAtOffset(0x1cc, PointerDataType.dataType, 4, "keyListener", null);
        s.replaceAtOffset(0x1d0, PointerDataType.dataType, 4, "mpApp", null);
        // touchIds[8] at +0x1d4
        for (int i = 0; i < 8; i++) {
            s.replaceAtOffset(0x1d4 + i * 4, IntegerDataType.dataType, 4, "touchId_" + i, null);
        }
        s.replaceAtOffset(0x1f4, IntegerDataType.dataType, 4, "m_NextTouchId", null);
        commit(s);
    }

    private void createFruitNinjaApp() {
        StructureDataType s = make("FruitNinjaApp", 0x48);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "field_0x24", null);
        s.replaceAtOffset(0x28, IntegerDataType.dataType, 4, "eglSurface", null);
        s.replaceAtOffset(0x38, IntegerDataType.dataType, 4, "field_0x38", null);
        s.replaceAtOffset(0x40, PointerDataType.dataType, 4, "mTimer", null);
        commit(s);
    }

    // ===================== TOUCH / INPUT =====================

    private void createTouchEvnt() {
        StructureDataType s = make("TouchEvnt", 0x14);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "touchId", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "pressed", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "x", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "y", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pressure", null);
        commit(s);
    }

    // ===================== SCREEN EFFECT / PURCHASE =====================

    private void createScreenEffect() {
        StructureDataType s = make("ScreenEffect", 0x50);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x44, PointerDataType.dataType, 4, "m_pOwnerPowerUp", null);
        commit(s);
    }

    private void createPurchaseInfo() {
        StructureDataType s = make("PurchaseInfo", 0xc4);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Cost", null);
        commit(s);
    }
}
