// ApplyStructs.java — Core structs (updated with GameInit findings)
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class FN01_ApplyStructs extends GhidraScript {
    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");
        int txId = currentProgram.startTransaction("Apply FruitNinja structs");
        try {
            // Basic types
            commit(makeVec3()); commit(makeQuat()); commit(makeColour());
            // Collision
            commit(makeColLine()); commit(makeColSphere());
            // Entities
            commit(makeEntity()); commit(makeFruit()); commit(makeBomb()); commit(makeSlashEntity());
            // Game
            commit(makeGame()); commit(makeGameTaskState());
            // Camera
            commit(makeMortarCamera()); commit(makeFruitCamera());
            // Wave
            commit(makeWaveManager()); commit(makeWaveInfo()); commit(makeDefaultWaveInfo());
            commit(makeCoinChance()); commit(makeSpawnerInfo());
            // HUD
            commit(makeHUD()); commit(makeHUDControl()); commit(makeMissControl());
            // Data
            commit(makeFruitInfo()); commit(makeImpactSound());
            commit(makeFruitPower()); commit(makeFruitPowers()); commit(makeFruitModelInfo());
            // Audio
            commit(makeMAMAudioThread());
            println("Done! All core structs created/updated.");
        } finally {
            currentProgram.endTransaction(txId, true);
        }
    }

    private void commit(StructureDataType s) { dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER); }
    private StructureDataType m(String n, int sz) { return new StructureDataType(cat, n, sz); }
    private ArrayDataType ca(int len) { return new ArrayDataType(CharDataType.dataType, len, 1); }

    // ---- Basic Types ----
    private StructureDataType makeVec3() {
        StructureDataType s = m("Vec3", 12);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "x", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "y", null);
        s.replaceAtOffset(8, FloatDataType.dataType, 4, "z", null);
        return s;
    }
    private StructureDataType makeQuat() {
        StructureDataType s = m("Quaternion", 16);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "a", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "b", null);
        s.replaceAtOffset(8, FloatDataType.dataType, 4, "c", null);
        s.replaceAtOffset(12, FloatDataType.dataType, 4, "d", null);
        return s;
    }
    private StructureDataType makeColour() {
        StructureDataType s = m("Colour", 4);
        s.replaceAtOffset(0, ByteDataType.dataType, 1, "b", null);
        s.replaceAtOffset(1, ByteDataType.dataType, 1, "g", null);
        s.replaceAtOffset(2, ByteDataType.dataType, 1, "r", null);
        s.replaceAtOffset(3, ByteDataType.dataType, 1, "a", null);
        return s;
    }
    // ---- Collision ----
    private StructureDataType makeColLine() {
        StructureDataType s = m("ColLine", 0x20);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "a_x", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "a_y", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "a_z", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "b_x", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "b_y", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "b_z", null);
        return s;
    }
    private StructureDataType makeColSphere() {
        StructureDataType s = m("ColSphere", 0x18);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "center_x", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "center_y", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "center_z", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "radius", null);
        return s;
    }
    // ---- Entity ----
    private StructureDataType makeEntity() {
        StructureDataType s = m("MortarEntity", 0x3c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "field_0x04", null);
        s.replaceAtOffset(0x08, UnsignedShortDataType.dataType, 2, "m_TrackerID", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "flags", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "vel_x", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "vel_y", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "vel_z", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "scale_x", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "scale_y", null);
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "scale_z", null);
        s.replaceAtOffset(0x36, UnsignedShortDataType.dataType, 2, "angle", null);
        s.replaceAtOffset(0x38, PointerDataType.dataType, 4, "m_Col", null);
        return s;
    }
    private StructureDataType makeFruit() {
        StructureDataType s = m("Fruit", 0x118);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x08, UnsignedShortDataType.dataType, 2, "m_TrackerID", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "flags", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "vel_x", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "vel_y", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "vel_z", null);
        s.replaceAtOffset(0x38, PointerDataType.dataType, 4, "m_Col", null);
        s.replaceAtOffset(0x3c, ByteDataType.dataType, 1, "m_FruitType", null);
        s.replaceAtOffset(0x3d, ByteDataType.dataType, 1, "m_bNoPowerUp", null);
        s.replaceAtOffset(0x40, PointerDataType.dataType, 4, "m_pEmitter1", null);
        s.replaceAtOffset(0x44, PointerDataType.dataType, 4, "m_pEmitter2", null);
        s.replaceAtOffset(0x48, FloatDataType.dataType, 4, "m_SlicePos_x", null);
        s.replaceAtOffset(0x4c, FloatDataType.dataType, 4, "m_SlicePos_y", null);
        s.replaceAtOffset(0x50, FloatDataType.dataType, 4, "m_SlicePos_z", null);
        s.replaceAtOffset(0x64, IntegerDataType.dataType, 4, "m_CollisionSize", null);
        s.replaceAtOffset(0x6c, FloatDataType.dataType, 4, "m_SliceTimer", null);
        s.replaceAtOffset(0x70, UnsignedShortDataType.dataType, 2, "m_SliceAngle", null);
        s.replaceAtOffset(0x74, FloatDataType.dataType, 4, "m_SliceImpulse", null);
        s.replaceAtOffset(0x78, IntegerDataType.dataType, 4, "m_SliceState", null);
        s.replaceAtOffset(0x7c, ByteDataType.dataType, 1, "m_bActive", null);
        s.replaceAtOffset(0x80, FloatDataType.dataType, 4, "m_ChuckDelay", null);
        s.replaceAtOffset(0x84, FloatDataType.dataType, 4, "m_RotAxis_x", null);
        s.replaceAtOffset(0x88, FloatDataType.dataType, 4, "m_RotAxis_y", null);
        s.replaceAtOffset(0x8c, FloatDataType.dataType, 4, "m_RotAxis_z", null);
        s.replaceAtOffset(0x90, IntegerDataType.dataType, 4, "m_PlayerIdx", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "m_TimeScale", null);
        s.replaceAtOffset(0x98, FloatDataType.dataType, 4, "m_ZPosition", null);
        s.replaceAtOffset(0x9c, FloatDataType.dataType, 4, "m_Gravity_x", null);
        s.replaceAtOffset(0xa0, FloatDataType.dataType, 4, "m_Gravity_y", null);
        s.replaceAtOffset(0xa4, FloatDataType.dataType, 4, "m_Gravity_z", null);
        s.replaceAtOffset(0xb4, ByteDataType.dataType, 1, "m_bSliced", null);
        s.replaceAtOffset(0xb8, FloatDataType.dataType, 4, "m_HalfB_pos_x", null);
        s.replaceAtOffset(0xbc, FloatDataType.dataType, 4, "m_HalfB_pos_y", null);
        s.replaceAtOffset(0xc0, FloatDataType.dataType, 4, "m_HalfB_pos_z", null);
        s.replaceAtOffset(0xc4, FloatDataType.dataType, 4, "m_HalfB_vel_x", null);
        s.replaceAtOffset(0xc8, FloatDataType.dataType, 4, "m_HalfB_vel_y", null);
        s.replaceAtOffset(0xcc, FloatDataType.dataType, 4, "m_HalfB_vel_z", null);
        s.replaceAtOffset(0xd0, FloatDataType.dataType, 4, "m_Rot1_a", null);
        s.replaceAtOffset(0xd4, FloatDataType.dataType, 4, "m_Rot1_b", null);
        s.replaceAtOffset(0xd8, FloatDataType.dataType, 4, "m_Rot1_c", null);
        s.replaceAtOffset(0xdc, FloatDataType.dataType, 4, "m_Rot1_d", null);
        s.replaceAtOffset(0xe0, FloatDataType.dataType, 4, "m_Rot2_a", null);
        s.replaceAtOffset(0xe4, FloatDataType.dataType, 4, "m_Rot2_b", null);
        s.replaceAtOffset(0xe8, FloatDataType.dataType, 4, "m_Rot2_c", null);
        s.replaceAtOffset(0xec, FloatDataType.dataType, 4, "m_Rot2_d", null);
        s.replaceAtOffset(0xf0, FloatDataType.dataType, 4, "m_RotVel1_x", null);
        s.replaceAtOffset(0xf4, FloatDataType.dataType, 4, "m_RotVel1_y", null);
        s.replaceAtOffset(0xf8, FloatDataType.dataType, 4, "m_RotVel1_z", null);
        s.replaceAtOffset(0xfc, FloatDataType.dataType, 4, "m_RotVel2_x", null);
        s.replaceAtOffset(0x100, FloatDataType.dataType, 4, "m_RotVel2_y", null);
        s.replaceAtOffset(0x104, FloatDataType.dataType, 4, "m_RotVel2_z", null);
        s.replaceAtOffset(0x10d, ByteDataType.dataType, 1, "m_bCriticalEligible", null);
        s.replaceAtOffset(0x110, FloatDataType.dataType, 4, "m_ScaleAnim", null);
        return s;
    }
    private StructureDataType makeBomb() {
        StructureDataType s = m("Bomb", 0xac);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "flags", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "vel_x", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "vel_y", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "vel_z", null);
        s.replaceAtOffset(0x38, PointerDataType.dataType, 4, "m_Col", null);
        s.replaceAtOffset(0x3c, FloatDataType.dataType, 4, "m_SpawnTimer", null);
        s.replaceAtOffset(0x64, IntegerDataType.dataType, 4, "field38_0x64", null);
        s.replaceAtOffset(0x68, ByteDataType.dataType, 1, "activeFlag", null);
        s.replaceAtOffset(0x70, ShortDataType.dataType, 2, "m_RotVelX", null);
        s.replaceAtOffset(0x72, ShortDataType.dataType, 2, "m_RotVelY", null);
        s.replaceAtOffset(0x74, ShortDataType.dataType, 2, "m_RotX", null);
        s.replaceAtOffset(0x76, ShortDataType.dataType, 2, "m_RotY", null);
        s.replaceAtOffset(0x7c, PointerDataType.dataType, 4, "m_pEmitter", null);
        s.replaceAtOffset(0x80, ByteDataType.dataType, 1, "movementFlag", null);
        s.replaceAtOffset(0x88, ByteDataType.dataType, 1, "m_bBombFlag88", null);
        s.replaceAtOffset(0x8c, FloatDataType.dataType, 4, "accelForce_x", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "accelForce_y", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "accelForce_z", null);
        s.replaceAtOffset(0xa4, FloatDataType.dataType, 4, "countdown", null);
        s.replaceAtOffset(0xa8, FloatDataType.dataType, 4, "speedMult", null);
        return s;
    }
    private StructureDataType makeSlashEntity() {
        StructureDataType s = m("SlashEntity", 0x184);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "flags", null);
        s.replaceAtOffset(0x36, UnsignedShortDataType.dataType, 2, "angle", null);
        s.replaceAtOffset(0x38, PointerDataType.dataType, 4, "m_Col", null);
        s.replaceAtOffset(0x3c, PointerDataType.dataType, 4, "m_TrailEmitter", null);
        s.replaceAtOffset(0x40, FloatDataType.dataType, 4, "m_Scale", null);
        s.replaceAtOffset(0x44, IntegerDataType.dataType, 4, "m_BaseColour", null);
        s.replaceAtOffset(0x48, IntegerDataType.dataType, 4, "m_HighlightColour", null);
        s.replaceAtOffset(0x4c, ByteDataType.dataType, 1, "m_bFlag4c", null);
        s.replaceAtOffset(0x50, IntegerDataType.dataType, 4, "m_SplitPoint", null);
        s.replaceAtOffset(0x58, IntegerDataType.dataType, 4, "m_PointCount", null);
        s.replaceAtOffset(0x5c, PointerDataType.dataType, 4, "m_pLeftBuffer", null);
        s.replaceAtOffset(0x60, PointerDataType.dataType, 4, "m_pRightBuffer", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_BladeDir_x", null);
        s.replaceAtOffset(0x68, FloatDataType.dataType, 4, "m_BladeDir_y", null);
        s.replaceAtOffset(0x6c, FloatDataType.dataType, 4, "m_BladeDir_z", null);
        s.replaceAtOffset(0x70, FloatDataType.dataType, 4, "m_TailPos_x", null);
        s.replaceAtOffset(0x74, FloatDataType.dataType, 4, "m_TailPos_y", null);
        s.replaceAtOffset(0x78, FloatDataType.dataType, 4, "m_TailPos_z", null);
        s.replaceAtOffset(0x7c, FloatDataType.dataType, 4, "m_HeadPos_x", null);
        s.replaceAtOffset(0x80, FloatDataType.dataType, 4, "m_HeadPos_y", null);
        s.replaceAtOffset(0x84, FloatDataType.dataType, 4, "m_HeadPos_z", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "m_LineLengthSq", null);
        s.replaceAtOffset(0x98, FloatDataType.dataType, 4, "m_SpeedScale", null);
        s.replaceAtOffset(0x9c, IntegerDataType.dataType, 4, "m_SliceCount", null);
        s.replaceAtOffset(0x124, FloatDataType.dataType, 4, "m_ComboTimer", null);
        s.replaceAtOffset(0x128, IntegerDataType.dataType, 4, "m_ComboCount", null);
        s.replaceAtOffset(0x12c, IntegerDataType.dataType, 4, "m_ComboEntityType", null);
        s.replaceAtOffset(0x130, PointerDataType.dataType, 4, "m_pComboCtrl", null);
        s.replaceAtOffset(0x134, FloatDataType.dataType, 4, "m_GhostTimer", null);
        s.replaceAtOffset(0x138, ByteDataType.dataType, 1, "m_bGhostActive", null);
        s.replaceAtOffset(0x13c, IntegerDataType.dataType, 4, "m_ColEntityA", null);
        s.replaceAtOffset(0x140, IntegerDataType.dataType, 4, "m_ColEntityB", null);
        s.replaceAtOffset(0x144, ByteDataType.dataType, 1, "m_bBladeActive", null);
        s.replaceAtOffset(0x148, FloatDataType.dataType, 4, "m_ComboScoreBase", null);
        s.replaceAtOffset(0x180, UnsignedShortDataType.dataType, 2, "m_AngleCopy", null);
        return s;
    }
    // ---- Game ----
    private StructureDataType makeGame() {
        StructureDataType s = m("Game", 0x608);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x02, ByteDataType.dataType, 1, "m_bGameRunning", null);
        s.replaceAtOffset(0x04, ByteDataType.dataType, 1, "gameMode", null);
        s.replaceAtOffset(0x05, ByteDataType.dataType, 1, "pauseFlag", null);
        s.replaceAtOffset(0x06, ByteDataType.dataType, 1, "gameStateFlag", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_TransitionTimer", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "m_BombHitTimer", null);
        s.replaceAtOffset(0x14, ByteDataType.dataType, 1, "comboCounter", null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "currentScore", null);
        s.replaceAtOffset(0x1c, ByteDataType.dataType, 1, "m_bUnsullied", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_CritTimer", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "m_ScoreThreshold", null);
        s.replaceAtOffset(0x35, ByteDataType.dataType, 1, "m_bSlowMotion", null);
        s.replaceAtOffset(0x38, FloatDataType.dataType, 4, "dt", null);
        s.replaceAtOffset(0x3c, PointerDataType.dataType, 4, "pHUD", null);
        s.replaceAtOffset(0x44, ByteDataType.dataType, 1, "isFirstPlay1", null);
        s.replaceAtOffset(0x45, ByteDataType.dataType, 1, "isFirstPlay2", null);
        s.replaceAtOffset(0x48, PointerDataType.dataType, 4, "pCamera", null);
        s.replaceAtOffset(0x4c, PointerDataType.dataType, 4, "pSaveData", null);
        s.replaceAtOffset(0x50, PointerDataType.dataType, 4, "pFont0", null);
        s.replaceAtOffset(0x54, PointerDataType.dataType, 4, "pFont1", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "worldPos_x", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "worldPos_y", null);
        s.replaceAtOffset(0x160, PointerDataType.dataType, 4, "pMainScreen", null);
        s.replaceAtOffset(0x164, PointerDataType.dataType, 4, "pGameOverScreen", null);
        s.replaceAtOffset(0x168, PointerDataType.dataType, 4, "pTutorialCtrl", null);
        s.replaceAtOffset(0x174, IntegerDataType.dataType, 4, "fruitTotal", null);
        s.replaceAtOffset(0x178, PointerDataType.dataType, 4, "pCoinCounter", null);
        s.replaceAtOffset(0x180, PointerDataType.dataType, 4, "pTimeCtrl", null);
        s.replaceAtOffset(0x184, IntegerDataType.dataType, 4, "m_field184", null);
        s.replaceAtOffset(0x188, PointerDataType.dataType, 4, "pGameSound", null);
        s.replaceAtOffset(0x194, IntegerDataType.dataType, 4, "m_FrameTimer", null);
        s.replaceAtOffset(0x1a0, FloatDataType.dataType, 4, "m_MenuReturnTimer", null);
        s.replaceAtOffset(0x604, ByteDataType.dataType, 1, "m_bFrameDirty", null);
        return s;
    }
    private StructureDataType makeGameTaskState() {
        StructureDataType s = m("GameTaskState", 0x118);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "pPauseScreen", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "m_flag0c", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "pMainScreen", null);
        // +0x24: SlashEntity*[16] = 0x40 bytes
        s.replaceAtOffset(0x64, PointerDataType.dataType, 4, "pSliceEffectList", null);
        s.replaceAtOffset(0xbc, PointerDataType.dataType, 4, "pSliceFxModel", null);
        s.replaceAtOffset(0xc0, PointerDataType.dataType, 4, "pSliceFxCritModel", null);
        s.replaceAtOffset(0xc8, PointerDataType.dataType, 4, "pSliceEffectPool", null);
        s.replaceAtOffset(0xcc, FloatDataType.dataType, 4, "bombHitPos_x", null);
        s.replaceAtOffset(0xd0, FloatDataType.dataType, 4, "bombHitPos_y", null);
        s.replaceAtOffset(0xd4, FloatDataType.dataType, 4, "bombHitPos_z", null);
        s.replaceAtOffset(0xd8, PointerDataType.dataType, 4, "m_pBombSound", null);
        s.replaceAtOffset(0xdc, FloatDataType.dataType, 4, "m_NotifyTimer", null);
        s.replaceAtOffset(0xf4, PointerDataType.dataType, 4, "pLoadingTexture", null);
        s.replaceAtOffset(0xf8, ByteDataType.dataType, 1, "m_bMenuBombHit", null);
        s.replaceAtOffset(0xfc, PointerDataType.dataType, 4, "pBackgroundTexture", null);
        s.replaceAtOffset(0x100, PointerDataType.dataType, 4, "pDeferredControl", null);
        s.replaceAtOffset(0x110, ByteDataType.dataType, 1, "m_flag110", null);
        s.replaceAtOffset(0x111, ByteDataType.dataType, 1, "m_flag111", null);
        s.replaceAtOffset(0x112, ByteDataType.dataType, 1, "m_bInitialized", null);
        s.replaceAtOffset(0x113, ByteDataType.dataType, 1, "m_flag113", null);
        s.replaceAtOffset(0x114, IntegerDataType.dataType, 4, "m_savedWaveSpeed", null);
        return s;
    }
    // ---- Camera ----
    private StructureDataType makeMortarCamera() {
        StructureDataType s = m("MortarCamera", 0x12c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "fns", null);
        s.replaceAtOffset(0xe4, FloatDataType.dataType, 4, "m_pos_x", null);
        s.replaceAtOffset(0xe8, FloatDataType.dataType, 4, "m_pos_y", null);
        s.replaceAtOffset(0xec, FloatDataType.dataType, 4, "m_pos_z", null);
        s.replaceAtOffset(0xf0, FloatDataType.dataType, 4, "m_lookAt_x", null);
        s.replaceAtOffset(0xf4, FloatDataType.dataType, 4, "m_lookAt_y", null);
        s.replaceAtOffset(0xf8, FloatDataType.dataType, 4, "m_lookAt_z", null);
        s.replaceAtOffset(0xfc, FloatDataType.dataType, 4, "m_up_x", null);
        s.replaceAtOffset(0x100, FloatDataType.dataType, 4, "m_up_y", null);
        s.replaceAtOffset(0x104, FloatDataType.dataType, 4, "m_up_z", null);
        s.replaceAtOffset(0x108, ByteDataType.dataType, 1, "m_bDirty", null);
        s.replaceAtOffset(0x124, FloatDataType.dataType, 4, "m_fovOrNear", null);
        s.replaceAtOffset(0x128, FloatDataType.dataType, 4, "m_farPlane", null);
        return s;
    }
    private StructureDataType makeFruitCamera() {
        StructureDataType s = m("FruitCamera", 0x16c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "fns", null);
        s.replaceAtOffset(0xe4, FloatDataType.dataType, 4, "m_pos_x", null);
        s.replaceAtOffset(0x130, IntegerDataType.dataType, 4, "m_CameraMode", null);
        s.replaceAtOffset(0x138, FloatDataType.dataType, 4, "m_ShakeDir_x", null);
        s.replaceAtOffset(0x13c, FloatDataType.dataType, 4, "m_ShakeDir_y", null);
        s.replaceAtOffset(0x140, UnsignedShortDataType.dataType, 2, "m_ShakeAngle", null);
        s.replaceAtOffset(0x144, FloatDataType.dataType, 4, "m_TargetX", null);
        s.replaceAtOffset(0x148, FloatDataType.dataType, 4, "m_TargetY", null);
        s.replaceAtOffset(0x154, FloatDataType.dataType, 4, "m_DistanceMag", null);
        s.replaceAtOffset(0x164, FloatDataType.dataType, 4, "m_ShakeIntensity", null);
        return s;
    }
    // ---- Wave ----
    private StructureDataType makeWaveManager() {
        StructureDataType s = m("WaveManager", 0x2d8);
        s.replaceAtOffset(0x54, FloatDataType.dataType, 4, "m_Speed_P0", null);
        s.replaceAtOffset(0x58, FloatDataType.dataType, 4, "m_Speed_P1", null);
        s.replaceAtOffset(0x68, FloatDataType.dataType, 4, "spawnLevel", null);
        s.replaceAtOffset(0x70, FloatDataType.dataType, 4, "m_CritChanceMult", null);
        s.replaceAtOffset(0x22c, PointerDataType.dataType, 4, "m_pCurrentWave_P0", null);
        s.replaceAtOffset(0x230, PointerDataType.dataType, 4, "m_pCurrentWave_P1", null);
        return s;
    }
    private StructureDataType makeWaveInfo() {
        StructureDataType s = m("WAVE_INFO", 0x78);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_ScoreThreshold", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_EndScore", null);
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pSpawners", null);
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "m_SpawnerCount", null);
        s.replaceAtOffset(0x38, ByteDataType.dataType, 1, "m_bAllowBombs", null);
        s.replaceAtOffset(0x44, FloatDataType.dataType, 4, "m_WaveDelay", null);
        s.replaceAtOffset(0x4c, IntegerDataType.dataType, 4, "m_BombMin", null);
        s.replaceAtOffset(0x50, IntegerDataType.dataType, 4, "m_BombMax", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_CriticalChance", null);
        s.replaceAtOffset(0x68, IntegerDataType.dataType, 4, "m_WaveIndex", null);
        s.replaceAtOffset(0x6c, PointerDataType.dataType, 4, "m_pCoinChance", null);
        s.replaceAtOffset(0x74, IntegerDataType.dataType, 4, "m_TotalWeight", null);
        return s;
    }
    private StructureDataType makeDefaultWaveInfo() {
        StructureDataType s = m("DEFAULT_WAVE_INFO", 0x40);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_DefaultCount", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_CritChance", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "m_WaveDelay", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_SpawnTimeScale", null);
        return s;
    }
    private StructureDataType makeCoinChance() {
        StructureDataType s = m("COIN_CHANCEINATOR", 8);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_Chance", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_MaxCoins", null);
        return s;
    }
    private StructureDataType makeSpawnerInfo() {
        StructureDataType s = m("SPAWNER_INFO", 0x64);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pFruitTypeHashes", null);
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "m_FruitTypeCount", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "m_TimeScale", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "m_Offset_x", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "m_Offset_y", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "m_Offset_z", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_MinAngle", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_MaxAngle", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_MinVel", null);
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "m_MaxVel", null);
        s.replaceAtOffset(0x34, ByteDataType.dataType, 1, "m_SpawnType", null);
        s.replaceAtOffset(0x38, FloatDataType.dataType, 4, "m_SpawnMin", null);
        s.replaceAtOffset(0x40, FloatDataType.dataType, 4, "m_SpawnMax", null);
        s.replaceAtOffset(0x44, FloatDataType.dataType, 4, "m_Speed", null);
        s.replaceAtOffset(0x48, FloatDataType.dataType, 4, "m_Gravity", null);
        s.replaceAtOffset(0x5c, FloatDataType.dataType, 4, "m_ZOffset", null);
        s.replaceAtOffset(0x60, ByteDataType.dataType, 1, "m_bForceOnce", null);
        return s;
    }
    // ---- HUD ----
    private StructureDataType makeHUD() {
        StructureDataType s = m("HUD", 0x24);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "controls_prev", null);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "controls_next", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "scale1", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "scale2", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "scale3", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "scale4", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "scale5", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "scale6", null);
        return s;
    }
    private StructureDataType makeHUDControl() {
        StructureDataType s = m("HUDControl", 0x60);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "pivot_x", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "pivot_y", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "pivot_z", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "size_x", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "size_y", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "size_z", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_Timer", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "m_bActive", null);
        s.replaceAtOffset(0x32, ByteDataType.dataType, 1, "m_bNoDestructor", null);
        s.replaceAtOffset(0x33, ByteDataType.dataType, 1, "m_bPendingRemoval", null);
        s.replaceAtOffset(0x34, IntegerDataType.dataType, 4, "m_LayerFlags", null);
        s.replaceAtOffset(0x5f, ByteDataType.dataType, 1, "m_Alpha", null);
        return s;
    }
    private StructureDataType makeMissControl() {
        StructureDataType s = m("MissControl", 0x94);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x7c, ByteDataType.dataType, 1, "m_AnimState", null);
        s.replaceAtOffset(0x7d, ByteDataType.dataType, 1, "m_bVisible", null);
        s.replaceAtOffset(0x80, FloatDataType.dataType, 4, "m_FadeAlpha", null);
        s.replaceAtOffset(0x84, ByteDataType.dataType, 1, "m_bComboActive", null);
        s.replaceAtOffset(0x88, IntegerDataType.dataType, 4, "m_ComboCount", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "m_AlphaScale", null);
        return s;
    }
    // ---- Data ----
    private StructureDataType makeFruitInfo() {
        StructureDataType s = m("FRUIT_INFO", 0x330);
        s.replaceAtOffset(0x000, ca(0x40), 0x40, "m_Name", null);
        s.replaceAtOffset(0x040, ca(0x40), 0x40, "m_AltTexName", null);
        s.replaceAtOffset(0x080, ca(0x40), 0x40, "m_ModelPath", null);
        s.replaceAtOffset(0x0c0, ca(0x40), 0x40, "m_LocalisedName", null);
        s.replaceAtOffset(0x100, ca(0x40), 0x40, "m_FactText", null);
        s.replaceAtOffset(0x140, ca(0x40), 0x40, "m_StatCategory", null);
        s.replaceAtOffset(0x180, ca(0x40), 0x40, "m_StatName2", null);
        s.replaceAtOffset(0x1c0, ca(0x40), 0x40, "m_StatName3", null);
        s.replaceAtOffset(0x200, ca(0x40), 0x40, "m_ExtraString", null);
        s.replaceAtOffset(0x240, IntegerDataType.dataType, 4, "m_FruitColour", null);
        s.replaceAtOffset(0x244, FloatDataType.dataType, 4, "m_Scale", null);
        s.replaceAtOffset(0x248, FloatDataType.dataType, 4, "m_SpeedMult", null);
        s.replaceAtOffset(0x24c, FloatDataType.dataType, 4, "m_SizeMult", null);
        s.replaceAtOffset(0x250, UnsignedIntegerDataType.dataType, 4, "m_NameHash", null);
        s.replaceAtOffset(0x254, UnsignedIntegerDataType.dataType, 4, "m_NameHashUpper", null);
        s.replaceAtOffset(0x258, UnsignedIntegerDataType.dataType, 4, "m_PatternHash1", null);
        s.replaceAtOffset(0x25c, UnsignedIntegerDataType.dataType, 4, "m_PatternHash2", null);
        s.replaceAtOffset(0x260, UnsignedIntegerDataType.dataType, 4, "m_StatCategoryHash", null);
        s.replaceAtOffset(0x270, IntegerDataType.dataType, 4, "m_FactCount", null);
        s.replaceAtOffset(0x274, PointerDataType.dataType, 4, "m_pFacts", null);
        s.replaceAtOffset(0x278, ca(0x40), 0x40, "m_FactTexPath", null);
        s.replaceAtOffset(0x2f8, IntegerDataType.dataType, 4, "m_FactColour", null);
        s.replaceAtOffset(0x300, PointerDataType.dataType, 4, "m_pFruitTexture", null);
        s.replaceAtOffset(0x304, PointerDataType.dataType, 4, "m_pFruitTexture2", null);
        s.replaceAtOffset(0x314, IntegerDataType.dataType, 4, "m_BaseScore", null);
        s.replaceAtOffset(0x318, ByteDataType.dataType, 1, "m_bScorable", null);
        s.replaceAtOffset(0x319, ByteDataType.dataType, 1, "m_bSpecial", null);
        s.replaceAtOffset(0x31c, PointerDataType.dataType, 4, "m_pSounds", null);
        s.replaceAtOffset(0x320, IntegerDataType.dataType, 4, "m_SoundCount", null);
        s.replaceAtOffset(0x324, IntegerDataType.dataType, 4, "m_RandBonusBase", null);
        s.replaceAtOffset(0x328, IntegerDataType.dataType, 4, "m_RandBonusMax", null);
        s.replaceAtOffset(0x32c, PointerDataType.dataType, 4, "m_pPowers", null);
        return s;
    }
    private StructureDataType makeImpactSound() {
        StructureDataType s = m("ImpactSound", 0xc);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_SoundName", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Weight", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_CumulativeWeight", null);
        return s;
    }
    private StructureDataType makeFruitPower() {
        StructureDataType s = m("FRUIT_POWER", 0xc);
        s.replaceAtOffset(0x00, UnsignedIntegerDataType.dataType, 4, "m_PowerHash", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Weight", null);
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType, 4, "m_CumulativeWeight", null);
        return s;
    }
    private StructureDataType makeFruitPowers() {
        StructureDataType s = m("FRUIT_POWERS", 8);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pArray", null);
        s.replaceAtOffset(0x04, UnsignedIntegerDataType.dataType, 4, "m_Count", null);
        return s;
    }
    private StructureDataType makeFruitModelInfo() {
        StructureDataType s = m("FruitModelInfo", 0x24);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pEffectPropA", null);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "m_pEffectPropB", null);
        s.replaceAtOffset(0x10, PointerDataType.dataType, 4, "m_pHalfModelA", null);
        s.replaceAtOffset(0x14, PointerDataType.dataType, 4, "m_pHalfModelB", null);
        s.replaceAtOffset(0x18, PointerDataType.dataType, 4, "m_pWholeModel", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pMultiplayerModel", null);
        return s;
    }
    // ---- Audio ----
    private StructureDataType makeMAMAudioThread() {
        StructureDataType s = m("MAMAudioThread", 0x154);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "masterVolume", null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "sampleRate", null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType, 4, "bufferSize", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "voiceCount", null);
        return s;
    }
}
