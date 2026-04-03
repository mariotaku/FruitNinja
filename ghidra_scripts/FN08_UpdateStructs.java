// FN08_UpdateStructs.java — Updates structs from session analysis
// Fixes FRUIT_INFO field names, adds FruitNinjaApp full layout,
// OspPoint, BadaSound, and modifier structs
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN08_UpdateStructs extends GhidraScript {
    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");
        int txId = currentProgram.startTransaction("FN08 Update structs");
        try {
            // Bada OS types
            commit(makeOspObject());
            commit(makeOspPoint());
            // FruitNinja app (replaces old FruitNinjaApp)
            commit(makeFruitNinjaApp());
            // FRUIT_INFO with corrected field names
            commit(makeFruitInfo());
            // BadaSound platform backend
            commit(makeBadaSound());
            // Power-up modifier structs
            commit(makeGameModifier());
            commit(makeScoreModifier());
            commit(makeTimeModifier());
            commit(makeSlashModifier());
            commit(makeWaveModifier());
            println("Done! Updated structs applied.");
        } finally {
            currentProgram.endTransaction(txId, true);
        }
    }

    private void commit(StructureDataType s) {
        dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
    }
    private StructureDataType m(String n, int sz) {
        return new StructureDataType(cat, n, sz);
    }
    private ArrayDataType ca(int len) {
        return new ArrayDataType(CharDataType.dataType, len, 1);
    }

    // ---- Bada OS types ----

    private StructureDataType makeOspObject() {
        StructureDataType s = m("OspObject", 4);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        return s;
    }

    private StructureDataType makeOspPoint() {
        // From bada_SDK/Include/FGrpPoint.h
        // Point : Osp::Base::Object { int x; int y; PointEx* __pPointEx; }
        StructureDataType s = m("OspPoint", 0x10);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", "from Osp::Base::Object");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "x", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "y", null);
        s.replaceAtOffset(0x0c, PointerDataType.dataType, 4, "__pPointEx", "private");
        return s;
    }

    // ---- FruitNinja App ----

    private StructureDataType makeFruitNinjaApp() {
        // 3 bases: Application(+0x00), IScreenEventListener(+0x0c), ITimerEventListener(+0x10)
        StructureDataType s = m("FruitNinjaApp", 0x48);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable_App", "Osp::App::Application");
        s.replaceAtOffset(0x0c, PointerDataType.dataType, 4, "vtable_Screen", "IScreenEventListener");
        s.replaceAtOffset(0x10, PointerDataType.dataType, 4, "vtable_Timer", "ITimerEventListener");
        s.replaceAtOffset(0x14, IntegerDataType.dataType, 4, "m_eglDisplay", null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "m_eglSurface", null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType, 4, "m_eglConfig", null);
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "m_eglContext", null);
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "field_0x24", null);
        s.replaceAtOffset(0x28, IntegerDataType.dataType, 4, "m_eglPbuffer", null);
        s.replaceAtOffset(0x3c, PointerDataType.dataType, 4, "m_pUnk3c", null);
        s.replaceAtOffset(0x40, PointerDataType.dataType, 4, "m_pTimer", "Osp::Base::Runtime::Timer*");
        s.replaceAtOffset(0x44, PointerDataType.dataType, 4, "m_pGlesForm", "GlesForm*");
        return s;
    }

    // ---- FRUIT_INFO with corrected names ----

    private StructureDataType makeFruitInfo() {
        StructureDataType s = m("FRUIT_INFO", 0x330);
        // String fields (9 x 0x40)
        s.replaceAtOffset(0x000, ca(0x40), 0x40, "m_Name", null);
        s.replaceAtOffset(0x040, ca(0x40), 0x40, "m_AltTexName", null);
        s.replaceAtOffset(0x080, ca(0x40), 0x40, "m_ModelPath", null);
        s.replaceAtOffset(0x0c0, ca(0x40), 0x40, "m_LocalisedName", null);
        s.replaceAtOffset(0x100, ca(0x40), 0x40, "m_FactText", null);
        s.replaceAtOffset(0x140, ca(0x40), 0x40, "m_StatCategory", null);
        s.replaceAtOffset(0x180, ca(0x40), 0x40, "m_StatName2", null);
        s.replaceAtOffset(0x1c0, ca(0x40), 0x40, "m_StatName3", null);
        s.replaceAtOffset(0x200, ca(0x40), 0x40, "m_ExtraString", null);
        // Colour
        s.replaceAtOffset(0x240, IntegerDataType.dataType, 4, "m_FruitColour", "BGRA");
        // Floats — CORRECTED names
        s.replaceAtOffset(0x244, FloatDataType.dataType, 4, "m_CollisionScale", "radius = base + const * scale");
        s.replaceAtOffset(0x248, FloatDataType.dataType, 4, "m_CollisionBase", "base collision radius");
        s.replaceAtOffset(0x24c, FloatDataType.dataType, 4, "m_SizeMult", null);
        // Hashes
        s.replaceAtOffset(0x250, UnsignedIntegerDataType.dataType, 4, "m_NameHash", null);
        s.replaceAtOffset(0x254, UnsignedIntegerDataType.dataType, 4, "m_NameHashUpper", null);
        s.replaceAtOffset(0x258, UnsignedIntegerDataType.dataType, 4, "m_PatternHash1", null);
        s.replaceAtOffset(0x25c, UnsignedIntegerDataType.dataType, 4, "m_PatternHash2", null);
        s.replaceAtOffset(0x260, UnsignedIntegerDataType.dataType, 4, "m_StatCategoryHash", null);
        s.replaceAtOffset(0x264, UnsignedIntegerDataType.dataType, 4, "m_StatName2Hash", null);
        s.replaceAtOffset(0x268, UnsignedIntegerDataType.dataType, 4, "m_StatName3Hash", null);
        // Flags
        s.replaceAtOffset(0x26c, ByteDataType.dataType, 1, "m_bFlag26c", null);
        // Facts
        s.replaceAtOffset(0x270, IntegerDataType.dataType, 4, "m_FactCount", null);
        s.replaceAtOffset(0x274, PointerDataType.dataType, 4, "m_pFacts", "char** (0x100 per string)");
        // Extra string 2
        s.replaceAtOffset(0x278, ca(0x80), 0x80, "m_ExtraString2", "from XML attr");
        // Second colour
        s.replaceAtOffset(0x2f8, IntegerDataType.dataType, 4, "m_SecondColour", "BGRA, alpha=0xFF");
        s.replaceAtOffset(0x2fc, ByteDataType.dataType, 1, "m_bFlag2fc", null);
        // Textures
        s.replaceAtOffset(0x300, PointerDataType.dataType, 4, "m_pFruitTexture", "SmartPtr<Texture>");
        s.replaceAtOffset(0x304, PointerDataType.dataType, 4, "m_pFruitTexture2", "SmartPtr<Texture>");
        // Int fields — m_Chance ADDED
        s.replaceAtOffset(0x308, IntegerDataType.dataType, 4, "m_Chance", "spawn weight for RandomFruit");
        s.replaceAtOffset(0x314, IntegerDataType.dataType, 4, "m_BaseScore", null);
        s.replaceAtOffset(0x318, ByteDataType.dataType, 1, "m_bScorable", null);
        s.replaceAtOffset(0x319, ByteDataType.dataType, 1, "m_bSpecial", null);
        // Sounds
        s.replaceAtOffset(0x31c, PointerDataType.dataType, 4, "m_pSounds", "ImpactSound*");
        s.replaceAtOffset(0x320, IntegerDataType.dataType, 4, "m_SoundCount", null);
        // Bonus
        s.replaceAtOffset(0x324, IntegerDataType.dataType, 4, "m_RandBonusBase", null);
        s.replaceAtOffset(0x328, IntegerDataType.dataType, 4, "m_RandBonusMax", null);
        // Powers
        s.replaceAtOffset(0x32c, PointerDataType.dataType, 4, "m_pPowers", "FRUIT_POWERS*");
        return s;
    }

    // ---- BadaSound ----

    private StructureDataType makeBadaSound() {
        StructureDataType s = m("BadaSound", 0x878);
        s.replaceAtOffset(0x000, PointerDataType.dataType, 4, "m_pOverlayCtrl", null);
        s.replaceAtOffset(0x004, PointerDataType.dataType, 4, "m_pPlayer", "Osp::Media::Player*");
        s.replaceAtOffset(0x008, FloatDataType.dataType, 4, "m_MusicVolume", "0.0-1.0");
        s.replaceAtOffset(0x00c, FloatDataType.dataType, 4, "m_SFXVolume", "0.0-1.0");
        // Hash table: 256 uints at +0x10..+0x410
        s.replaceAtOffset(0x010, new ArrayDataType(UnsignedIntegerDataType.dataType, 256, 4), 1024,
                          "m_SoundHashes", "StringHash[256]");
        // Effect pointers: 256 ptrs at +0x410..+0x810
        s.replaceAtOffset(0x410, new ArrayDataType(PointerDataType.dataType, 256, 4), 1024,
                          "m_SoundEffects", "SoundEffectBada*[256]");
        s.replaceAtOffset(0x810, IntegerDataType.dataType, 4, "m_SoundCount", "max 256");
        // 8 active slots at +0x814, each 0x0c bytes
        s.replaceAtOffset(0x814, new ArrayDataType(ByteDataType.dataType, 0x60, 1), 0x60,
                          "m_ActiveSlots", "ActiveEffect[8] × 0x0c each");
        return s;
    }

    // ---- Power-up Modifier structs ----

    private StructureDataType makeGameModifier() {
        StructureDataType s = m("GameModifier", 0x20);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Duration", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "m_TimeRemaining", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_TotalTime", null);
        s.replaceAtOffset(0x18, ByteDataType.dataType, 1, "m_bApplied", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pOwner", "PowerUp*");
        return s;
    }

    private StructureDataType makeScoreModifier() {
        StructureDataType s = m("ScoreModifier", 0x3c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Duration", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pOwner", "PowerUp*");
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "m_ScoreGainAdd", "init=0");
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "m_ScoreGainMultiply", "init=1");
        s.replaceAtOffset(0x28, IntegerDataType.dataType, 4, "m_ScoreLossAdd", "init=0");
        s.replaceAtOffset(0x2c, IntegerDataType.dataType, 4, "m_ScoreLossMultiply", "init=1");
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "m_ApplyCount", null);
        s.replaceAtOffset(0x34, ByteDataType.dataType, 1, "m_bApplied", "one-shot vs continuous");
        return s;
    }

    private StructureDataType makeTimeModifier() {
        StructureDataType s = m("TimeModifier", 0x3c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Duration", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_TotalTime", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pOwner", "PowerUp*");
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "m_RampTarget", "default 1.0");
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_RampDuration", "default 0.0");
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_CurrentScale", "default 1.0");
        s.replaceAtOffset(0x2c, ByteDataType.dataType, 1, "m_bStopClock", null);
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "m_TimeScale", "clock multiplier");
        s.replaceAtOffset(0x34, FloatDataType.dataType, 4, "m_AddTime", "one-shot time bonus");
        s.replaceAtOffset(0x38, IntegerDataType.dataType, 4, "m_AddTimeCountdown", "1=pending");
        return s;
    }

    private StructureDataType makeSlashModifier() {
        StructureDataType s = m("SlashModifier", 0x40);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Duration", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pOwner", "PowerUp*");
        s.replaceAtOffset(0x20, PointerDataType.dataType, 4, "m_pColours", "Colour* heap array");
        s.replaceAtOffset(0x24, IntegerDataType.dataType, 4, "m_ColourCount", null);
        s.replaceAtOffset(0x28, IntegerDataType.dataType, 4, "m_ColourType", "ParseSlashModColourType");
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_Width", "blade width, default 1.0");
        s.replaceAtOffset(0x30, PointerDataType.dataType, 4, "m_pTexture", "cloned string");
        s.replaceAtOffset(0x34, PointerDataType.dataType, 4, "m_pFxTexture", "0x40 buffer");
        s.replaceAtOffset(0x38, UnsignedIntegerDataType.dataType, 4, "m_PowerMask", "OR'd bitmask");
        s.replaceAtOffset(0x3c, ByteDataType.dataType, 1, "m_bApplied", null);
        return s;
    }

    private StructureDataType makeWaveModifier() {
        StructureDataType s = m("WaveModifier", 0x44);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_Duration", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pOwner", "PowerUp*");
        // +0x20: std::vector<PROBABILITY_OVERIDE> (12 bytes)
        s.replaceAtOffset(0x20, PointerDataType.dataType, 4, "m_Overrides_begin", null);
        s.replaceAtOffset(0x24, PointerDataType.dataType, 4, "m_Overrides_end", null);
        s.replaceAtOffset(0x28, PointerDataType.dataType, 4, "m_Overrides_capacity", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_BombMultiplier", "default 1.0");
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "m_BombScale", "default 1.0");
        s.replaceAtOffset(0x34, FloatDataType.dataType, 4, "m_FruitMultiplier", "default 1.0");
        s.replaceAtOffset(0x38, FloatDataType.dataType, 4, "m_PowerupDtMod", "default 1.0");
        s.replaceAtOffset(0x3c, IntegerDataType.dataType, 4, "m_SetWave", "10000 = don't set");
        s.replaceAtOffset(0x40, FloatDataType.dataType, 4, "m_CriticalChanceMod", "default 1.0");
        return s;
    }
}
