// ApplyStructs.java — Creates recovered struct data types in Ghidra
// Run from Script Manager after opening FruitNinja.exe
// @category FruitNinja

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;

public class ApplyStructs extends GhidraScript {

    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/FruitNinja");

        int txId = currentProgram.startTransaction("Apply FruitNinja structs");
        try {
            createVec3();
            createQuaternion();
            createColour();
            createEntity();
            createFruit();
            createBomb();
            createSlashEntity();
            createGame();
            createFruitCamera();
            createMortarCamera();
            createWaveManager();
            createHUD();
            createHUDControl();
            createMissControl();
            createImpactSound();
            createFruitPower();
            createFruitPowers();
            createFruitModelInfo();
            createFruitInfo();
            createColLine();
            createColSphere();
            createMAMAudioThread();
            createSpawnerInfo();
            createWaveInfo();
            createDefaultWaveInfo();
            createCoinChanceinator();
            println("Done! All structs created under /FruitNinja category.");
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

    private ArrayDataType charArray(int len) {
        return new ArrayDataType(CharDataType.dataType, len, 1);
    }

    // ===================== BASIC TYPES =====================

    private void createVec3() {
        StructureDataType s = make("Vec3", 12);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "x", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "y", null);
        s.replaceAtOffset(8, FloatDataType.dataType, 4, "z", null);
        commit(s);
    }

    private void createQuaternion() {
        StructureDataType s = make("Quaternion", 16);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "a", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "b", null);
        s.replaceAtOffset(8, FloatDataType.dataType, 4, "c", null);
        s.replaceAtOffset(12, FloatDataType.dataType, 4, "d", null);
        commit(s);
    }

    private void createColour() {
        StructureDataType s = make("Colour", 4);
        s.replaceAtOffset(0, ByteDataType.dataType, 1, "b", null);
        s.replaceAtOffset(1, ByteDataType.dataType, 1, "g", null);
        s.replaceAtOffset(2, ByteDataType.dataType, 1, "r", null);
        s.replaceAtOffset(3, ByteDataType.dataType, 1, "a", null);
        commit(s);
    }

    // ===================== COLLISION =====================

    private void createColLine() {
        StructureDataType s = make("ColLine", 0x20);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "a_x", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "a_y", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "a_z", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "b_x", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "b_y", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "b_z", null);
        commit(s);
    }

    private void createColSphere() {
        StructureDataType s = make("ColSphere", 0x18);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "center_x", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "center_y", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "center_z", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "radius", null);
        commit(s);
    }

    // ===================== ENTITY BASE =====================

    private void createEntity() {
        StructureDataType s = make("MortarEntity", 0x3c);
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
        commit(s);
    }

    // ===================== FRUIT =====================

    private void createFruit() {
        StructureDataType s = make("Fruit", 0x118);
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
        s.replaceAtOffset(0x38, PointerDataType.dataType, 4, "m_Col", "ColSphere*");
        s.replaceAtOffset(0x3c, ByteDataType.dataType, 1, "m_FruitType", null);
        s.replaceAtOffset(0x3d, ByteDataType.dataType, 1, "m_bNoPowerUp", null);
        s.replaceAtOffset(0x40, PointerDataType.dataType, 4, "m_pEmitter1", null);
        s.replaceAtOffset(0x44, PointerDataType.dataType, 4, "m_pEmitter2", null);
        s.replaceAtOffset(0x48, FloatDataType.dataType, 4, "m_SlicePos_x", null);
        s.replaceAtOffset(0x4c, FloatDataType.dataType, 4, "m_SlicePos_y", null);
        s.replaceAtOffset(0x50, FloatDataType.dataType, 4, "m_SlicePos_z", null);
        s.replaceAtOffset(0x60, IntegerDataType.dataType, 4, "m_field60", null);
        s.replaceAtOffset(0x64, IntegerDataType.dataType, 4, "m_CollisionSize", null);
        s.replaceAtOffset(0x68, IntegerDataType.dataType, 4, "m_field68", null);
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
        s.replaceAtOffset(0x108, IntegerDataType.dataType, 4, "m_field108", null);
        s.replaceAtOffset(0x10c, ByteDataType.dataType, 1, "m_field10c", null);
        s.replaceAtOffset(0x10d, ByteDataType.dataType, 1, "m_bCriticalEligible", null);
        s.replaceAtOffset(0x110, FloatDataType.dataType, 4, "m_ScaleAnim", null);
        s.replaceAtOffset(0x114, ByteDataType.dataType, 1, "m_field114", null);
        commit(s);
    }

    // ===================== BOMB =====================

    private void createBomb() {
        StructureDataType s = make("Bomb", 0xac);
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
        s.replaceAtOffset(0x64, IntegerDataType.dataType, 4, "field38_0x64", null);
        s.replaceAtOffset(0x68, ByteDataType.dataType, 1, "activeFlag", null);
        s.replaceAtOffset(0x80, ByteDataType.dataType, 1, "movementFlag", null);
        s.replaceAtOffset(0x88, ByteDataType.dataType, 1, "m_bBombFlag88", null);
        s.replaceAtOffset(0x8c, FloatDataType.dataType, 4, "accelForce_x", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "accelForce_y", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "accelForce_z", null);
        s.replaceAtOffset(0xa4, FloatDataType.dataType, 4, "countdown", null);
        s.replaceAtOffset(0xa8, FloatDataType.dataType, 4, "speedMult", null);
        commit(s);
    }

    // ===================== SLASH ENTITY =====================

    private void createSlashEntity() {
        StructureDataType s = make("SlashEntity", 0x184);
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
        s.replaceAtOffset(0xa0, FloatDataType.dataType, 4, "m_SliceTimerA", null);
        s.replaceAtOffset(0xa4, FloatDataType.dataType, 4, "m_SliceTimerB", null);
        s.replaceAtOffset(0xa8, FloatDataType.dataType, 4, "m_BladeVelAtSlice_x", null);
        s.replaceAtOffset(0xac, FloatDataType.dataType, 4, "m_BladeVelAtSlice_y", null);
        s.replaceAtOffset(0xb0, FloatDataType.dataType, 4, "m_BladeVelAtSlice_z", null);
        s.replaceAtOffset(0xb4, FloatDataType.dataType, 4, "m_SlicePos_x", null);
        s.replaceAtOffset(0xb8, FloatDataType.dataType, 4, "m_SlicePos_y", null);
        s.replaceAtOffset(0xbc, FloatDataType.dataType, 4, "m_SlicePos_z", null);
        s.replaceAtOffset(0xc0, IntegerDataType.dataType, 4, "m_SliceEntityType", null);
        s.replaceAtOffset(0xc4, FloatDataType.dataType, 4, "m_ScoreBonus", null);
        for (int i = 0; i < 6; i++) {
            int off = 0xc8 + i * 12;
            s.replaceAtOffset(off, FloatDataType.dataType, 4, "m_Ghost" + i + "_x", null);
            s.replaceAtOffset(off+4, FloatDataType.dataType, 4, "m_Ghost" + i + "_y", null);
            s.replaceAtOffset(off+8, FloatDataType.dataType, 4, "m_Ghost" + i + "_z", null);
        }
        s.replaceAtOffset(0x110, IntegerDataType.dataType, 4, "m_GhostCount", null);
        s.replaceAtOffset(0x114, IntegerDataType.dataType, 4, "m_GhostFlags", null);
        s.replaceAtOffset(0x118, FloatDataType.dataType, 4, "m_GhostDir_x", null);
        s.replaceAtOffset(0x11c, FloatDataType.dataType, 4, "m_GhostDir_y", null);
        s.replaceAtOffset(0x120, FloatDataType.dataType, 4, "m_GhostDir_z", null);
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
        s.replaceAtOffset(0x14c, IntegerDataType.dataType, 4, "m_ExtraFieldA", null);
        s.replaceAtOffset(0x150, IntegerDataType.dataType, 4, "m_ExtraFieldB", null);
        for (int i = 0; i < 11; i++) {
            s.replaceAtOffset(0x154 + i * 4, IntegerDataType.dataType, 4, "m_ComboFruitID_" + i, null);
        }
        s.replaceAtOffset(0x180, UnsignedShortDataType.dataType, 2, "m_AngleCopy", null);
        commit(s);
    }

    // ===================== GAME =====================

    private void createGame() {
        StructureDataType s = make("Game", 0x608);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x02, ByteDataType.dataType, 1, "m_bGameRunning", null);
        s.replaceAtOffset(0x04, ByteDataType.dataType, 1, "gameMode", null);
        s.replaceAtOffset(0x05, ByteDataType.dataType, 1, "pauseFlag", null);
        s.replaceAtOffset(0x06, ByteDataType.dataType, 1, "gameStateFlag", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "field_0x08", null);
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
        s.replaceAtOffset(0x58, PointerDataType.dataType, 4, "pFont2", null);
        s.replaceAtOffset(0x5c, PointerDataType.dataType, 4, "pFont3", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "worldPos_x", null);
        s.replaceAtOffset(0x94, FloatDataType.dataType, 4, "worldPos_y", null);
        s.replaceAtOffset(0x160, PointerDataType.dataType, 4, "pMainScreen", null);
        s.replaceAtOffset(0x164, PointerDataType.dataType, 4, "pGameOverScreen", null);
        s.replaceAtOffset(0x174, IntegerDataType.dataType, 4, "fruitTotal", null);
        s.replaceAtOffset(0x188, PointerDataType.dataType, 4, "pGameSound", null);
        s.replaceAtOffset(0x194, IntegerDataType.dataType, 4, "m_FrameTimer", null);
        s.replaceAtOffset(0x1a0, FloatDataType.dataType, 4, "m_MenuReturnTimer", null);
        s.replaceAtOffset(0x604, ByteDataType.dataType, 1, "m_bFrameDirty", null);
        commit(s);
    }

    // ===================== CAMERAS =====================

    private void createMortarCamera() {
        StructureDataType s = make("MortarCamera", 0x12c);
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
        s.replaceAtOffset(0x109, ByteDataType.dataType, 1, "m_bInitialized", null);
        s.replaceAtOffset(0x11c, FloatDataType.dataType, 4, "m_nearX", null);
        s.replaceAtOffset(0x120, FloatDataType.dataType, 4, "m_nearY", null);
        s.replaceAtOffset(0x124, FloatDataType.dataType, 4, "m_fovOrNear", null);
        s.replaceAtOffset(0x128, FloatDataType.dataType, 4, "m_farPlane", null);
        commit(s);
    }

    private void createFruitCamera() {
        StructureDataType s = make("FruitCamera", 0x16c);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "fns", null);
        s.replaceAtOffset(0xe4, FloatDataType.dataType, 4, "m_pos_x", null);
        s.replaceAtOffset(0xe8, FloatDataType.dataType, 4, "m_pos_y", null);
        s.replaceAtOffset(0xec, FloatDataType.dataType, 4, "m_pos_z", null);
        s.replaceAtOffset(0x108, ByteDataType.dataType, 1, "m_bDirty", null);
        s.replaceAtOffset(0x12c, PointerDataType.dataType, 4, "m_pFollowFns", null);
        s.replaceAtOffset(0x130, IntegerDataType.dataType, 4, "m_CameraMode", null);
        s.replaceAtOffset(0x134, UnsignedShortDataType.dataType, 2, "m_field134", null);
        s.replaceAtOffset(0x136, UnsignedShortDataType.dataType, 2, "m_field136", null);
        s.replaceAtOffset(0x138, FloatDataType.dataType, 4, "m_ShakeDir_x", null);
        s.replaceAtOffset(0x13c, FloatDataType.dataType, 4, "m_ShakeDir_y", null);
        s.replaceAtOffset(0x140, UnsignedShortDataType.dataType, 2, "m_ShakeAngle", null);
        s.replaceAtOffset(0x144, FloatDataType.dataType, 4, "m_TargetX", null);
        s.replaceAtOffset(0x148, FloatDataType.dataType, 4, "m_TargetY", null);
        s.replaceAtOffset(0x14c, FloatDataType.dataType, 4, "m_field14c", null);
        s.replaceAtOffset(0x150, FloatDataType.dataType, 4, "m_field150", null);
        s.replaceAtOffset(0x154, FloatDataType.dataType, 4, "m_DistanceMag", null);
        s.replaceAtOffset(0x158, FloatDataType.dataType, 4, "m_LookAtSnap_x", null);
        s.replaceAtOffset(0x15c, FloatDataType.dataType, 4, "m_LookAtSnap_y", null);
        s.replaceAtOffset(0x160, FloatDataType.dataType, 4, "m_LookAtSnap_z", null);
        s.replaceAtOffset(0x164, FloatDataType.dataType, 4, "m_ShakeIntensity", null);
        s.replaceAtOffset(0x168, PointerDataType.dataType, 4, "m_field168", null);
        commit(s);
    }

    // ===================== WAVE SYSTEM =====================

    private void createWaveManager() {
        StructureDataType s = make("WaveManager", 0x2d8);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_CurrentState", null);
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pRandom", null);
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "m_field20", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_field24", null);
        s.replaceAtOffset(0x54, FloatDataType.dataType, 4, "m_Speed_P0", null);
        s.replaceAtOffset(0x58, FloatDataType.dataType, 4, "m_Speed_P1", null);
        s.replaceAtOffset(0x68, FloatDataType.dataType, 4, "spawnLevel", null);
        s.replaceAtOffset(0x6c, FloatDataType.dataType, 4, "m_field6c", null);
        s.replaceAtOffset(0x70, FloatDataType.dataType, 4, "m_CritChanceMult", null);
        // 4 DEFAULT_WAVE_INFO at +0xdc, each 0x40 bytes
        s.replaceAtOffset(0x22c, PointerDataType.dataType, 4, "m_pCurrentWave_P0", null);
        s.replaceAtOffset(0x230, PointerDataType.dataType, 4, "m_pCurrentWave_P1", null);
        s.replaceAtOffset(0x240, IntegerDataType.dataType, 4, "m_field240", null);
        s.replaceAtOffset(0x2d4, FloatDataType.dataType, 4, "field_0x2d4", null);
        commit(s);
    }

    private void createWaveInfo() {
        StructureDataType s = make("WAVE_INFO", 0x78);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_ScoreThreshold", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_EndScore", "-2=none");
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pSpawners", "SPAWNER_INFO*");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType, 4, "m_SpawnerCount", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "m_BombScale1", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "m_BombScale2", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "m_BombGravity", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "m_BombSpeed", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "m_BombSpeedMax", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_BombMinAngle", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_BombMaxAngle", null);
        s.replaceAtOffset(0x30, FloatDataType.dataType, 4, "m_BombField30", null);
        s.replaceAtOffset(0x38, ByteDataType.dataType, 1, "m_bAllowBombs", null);
        s.replaceAtOffset(0x39, ByteDataType.dataType, 1, "m_bAllowBombsFrenzy", null);
        s.replaceAtOffset(0x3c, IntegerDataType.dataType, 4, "m_MinScore", null);
        s.replaceAtOffset(0x44, FloatDataType.dataType, 4, "m_WaveDelay", null);
        s.replaceAtOffset(0x4c, IntegerDataType.dataType, 4, "m_BombMin", null);
        s.replaceAtOffset(0x50, IntegerDataType.dataType, 4, "m_BombMax", null);
        // +0x54: vector<string> m_SpecialFruits (12 bytes)
        s.replaceAtOffset(0x60, IntegerDataType.dataType, 4, "m_field60", null);
        s.replaceAtOffset(0x64, FloatDataType.dataType, 4, "m_CriticalChance", null);
        s.replaceAtOffset(0x68, IntegerDataType.dataType, 4, "m_WaveIndex", null);
        s.replaceAtOffset(0x6c, PointerDataType.dataType, 4, "m_pCoinChance", "COIN_CHANCEINATOR*");
        s.replaceAtOffset(0x70, IntegerDataType.dataType, 4, "m_WaveNumber", null);
        s.replaceAtOffset(0x74, IntegerDataType.dataType, 4, "m_TotalWeight", null);
        commit(s);
    }

    private void createDefaultWaveInfo() {
        StructureDataType s = make("DEFAULT_WAVE_INFO", 0x40);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_DefaultCount", null);
        s.replaceAtOffset(0x04, FloatDataType.dataType, 4, "m_CritChance", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "m_WaveDelay", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "m_SpawnTimeScale", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "m_BombScale", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "m_BombGravity", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "m_BombSpeed", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "m_BombSpeedMax", null);
        s.replaceAtOffset(0x20, FloatDataType.dataType, 4, "m_BombMin", null);
        s.replaceAtOffset(0x24, FloatDataType.dataType, 4, "m_BombMax", null);
        s.replaceAtOffset(0x28, FloatDataType.dataType, 4, "m_CritChanceMod", null);
        s.replaceAtOffset(0x2c, FloatDataType.dataType, 4, "m_field2c", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "m_field30", null);
        s.replaceAtOffset(0x34, ByteDataType.dataType, 1, "m_bAllowBombs", null);
        s.replaceAtOffset(0x35, ByteDataType.dataType, 1, "m_bAllowBombsFrenzy", null);
        commit(s);
    }

    private void createCoinChanceinator() {
        StructureDataType s = make("COIN_CHANCEINATOR", 8);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "m_Chance", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_MaxCoins", null);
        commit(s);
    }

    private void createSpawnerInfo() {
        StructureDataType s = make("SPAWNER_INFO", 0x64);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pFruitTypeHashes", "int* array");
        // +0x04: vector<string> m_FruitTypeNames (12 bytes)
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
        s.replaceAtOffset(0x4c, FloatDataType.dataType, 4, "m_field4c", null);
        s.replaceAtOffset(0x5c, FloatDataType.dataType, 4, "m_ZOffset", null);
        s.replaceAtOffset(0x60, ByteDataType.dataType, 1, "m_bForceOnce", null);
        commit(s);
    }

    // ===================== HUD SYSTEM =====================

    private void createHUD() {
        StructureDataType s = make("HUD", 0x20);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "controls_prev", null);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "controls_next", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "scale1", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "scale2", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "scale3", null);
        s.replaceAtOffset(0x14, FloatDataType.dataType, 4, "scale4", null);
        s.replaceAtOffset(0x18, FloatDataType.dataType, 4, "scale5", null);
        s.replaceAtOffset(0x1c, FloatDataType.dataType, 4, "scale6", null);
        commit(s);
    }

    private void createHUDControl() {
        StructureDataType s = make("HUDControl", 0x60);
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
        s.replaceAtOffset(0x5c, IntegerDataType.dataType, 4, "m_ColourPacked", null);
        s.replaceAtOffset(0x5f, ByteDataType.dataType, 1, "m_Alpha", null);
        commit(s);
    }

    private void createMissControl() {
        StructureDataType s = make("MissControl", 0x94);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x08, FloatDataType.dataType, 4, "pos_x", null);
        s.replaceAtOffset(0x0c, FloatDataType.dataType, 4, "pos_y", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "pos_z", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "m_bActive_base", null);
        s.replaceAtOffset(0x7c, ByteDataType.dataType, 1, "m_AnimState", null);
        s.replaceAtOffset(0x7d, ByteDataType.dataType, 1, "m_bVisible", null);
        s.replaceAtOffset(0x7e, UnsignedShortDataType.dataType, 2, "m_field7e", null);
        s.replaceAtOffset(0x80, FloatDataType.dataType, 4, "m_FadeAlpha", null);
        s.replaceAtOffset(0x84, ByteDataType.dataType, 1, "m_bComboActive", null);
        s.replaceAtOffset(0x85, ByteDataType.dataType, 1, "m_bFlag85", null);
        s.replaceAtOffset(0x88, IntegerDataType.dataType, 4, "m_ComboCount", null);
        s.replaceAtOffset(0x8c, ByteDataType.dataType, 1, "m_bFlag8c", null);
        s.replaceAtOffset(0x90, FloatDataType.dataType, 4, "m_AlphaScale", null);
        commit(s);
    }

    // ===================== FRUIT_INFO AND SUB-STRUCTS =====================

    private void createImpactSound() {
        StructureDataType s = make("ImpactSound", 0xc);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_SoundName", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Weight", null);
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "m_CumulativeWeight", null);
        commit(s);
    }

    private void createFruitPower() {
        StructureDataType s = make("FRUIT_POWER", 0xc);
        s.replaceAtOffset(0x00, UnsignedIntegerDataType.dataType, 4, "m_PowerHash", null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "m_Weight", null);
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType, 4, "m_CumulativeWeight", null);
        commit(s);
    }

    private void createFruitPowers() {
        StructureDataType s = make("FRUIT_POWERS", 8);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pArray", null);
        s.replaceAtOffset(0x04, UnsignedIntegerDataType.dataType, 4, "m_Count", null);
        commit(s);
    }

    private void createFruitModelInfo() {
        StructureDataType s = make("FruitModelInfo", 0x24);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "m_pEffectPropA", null);
        s.replaceAtOffset(0x04, PointerDataType.dataType, 4, "m_pEffectPropB", null);
        s.replaceAtOffset(0x08, PointerDataType.dataType, 4, "m_pColourVariantA", null);
        s.replaceAtOffset(0x0c, PointerDataType.dataType, 4, "m_pColourVariantB", null);
        s.replaceAtOffset(0x10, PointerDataType.dataType, 4, "m_pHalfModelA", null);
        s.replaceAtOffset(0x14, PointerDataType.dataType, 4, "m_pHalfModelB", null);
        s.replaceAtOffset(0x18, PointerDataType.dataType, 4, "m_pWholeModel", null);
        s.replaceAtOffset(0x1c, PointerDataType.dataType, 4, "m_pMultiplayerModel", null);
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "m_field20", null);
        commit(s);
    }

    private void createFruitInfo() {
        StructureDataType s = make("FRUIT_INFO", 0x330);
        // 10 string fields (char[64] each)
        s.replaceAtOffset(0x000, charArray(0x40), 0x40, "m_Name", null);
        s.replaceAtOffset(0x040, charArray(0x40), 0x40, "m_AltTexName", null);
        s.replaceAtOffset(0x080, charArray(0x40), 0x40, "m_ModelPath", null);
        s.replaceAtOffset(0x0c0, charArray(0x40), 0x40, "m_LocalisedName", null);
        s.replaceAtOffset(0x100, charArray(0x40), 0x40, "m_FactText", null);
        s.replaceAtOffset(0x140, charArray(0x40), 0x40, "m_StatCategory", null);
        s.replaceAtOffset(0x180, charArray(0x40), 0x40, "m_StatName2", null);
        s.replaceAtOffset(0x1c0, charArray(0x40), 0x40, "m_StatName3", null);
        s.replaceAtOffset(0x200, charArray(0x40), 0x40, "m_ExtraString", null);
        // Colour + floats
        s.replaceAtOffset(0x240, IntegerDataType.dataType, 4, "m_FruitColour", null);
        s.replaceAtOffset(0x244, FloatDataType.dataType, 4, "m_Scale", null);
        s.replaceAtOffset(0x248, FloatDataType.dataType, 4, "m_SpeedMult", null);
        s.replaceAtOffset(0x24c, FloatDataType.dataType, 4, "m_SizeMult", null);
        // Hashes
        s.replaceAtOffset(0x250, UnsignedIntegerDataType.dataType, 4, "m_NameHash", null);
        s.replaceAtOffset(0x254, UnsignedIntegerDataType.dataType, 4, "m_NameHashUpper", null);
        s.replaceAtOffset(0x258, UnsignedIntegerDataType.dataType, 4, "m_PatternHash1", null);
        s.replaceAtOffset(0x25c, UnsignedIntegerDataType.dataType, 4, "m_PatternHash2", null);
        s.replaceAtOffset(0x260, UnsignedIntegerDataType.dataType, 4, "m_StatCategoryHash", null);
        s.replaceAtOffset(0x264, UnsignedIntegerDataType.dataType, 4, "m_StatName2Hash", null);
        s.replaceAtOffset(0x268, UnsignedIntegerDataType.dataType, 4, "m_StatName3Hash", null);
        s.replaceAtOffset(0x26c, ByteDataType.dataType, 1, "m_bFlag26c", null);
        // Fact strings
        s.replaceAtOffset(0x270, IntegerDataType.dataType, 4, "m_FactCount", null);
        s.replaceAtOffset(0x274, PointerDataType.dataType, 4, "m_pFacts", null);
        // Fact texture path
        s.replaceAtOffset(0x278, charArray(0x40), 0x40, "m_FactTexPath", null);
        // Unknown region
        s.replaceAtOffset(0x2b8, charArray(0x40), 0x40, "m_UnknownStr2b8", null);
        // Second colour
        s.replaceAtOffset(0x2f8, IntegerDataType.dataType, 4, "m_FactColour", null);
        s.replaceAtOffset(0x2fc, ByteDataType.dataType, 1, "m_bFlag2fc", null);
        // Textures
        s.replaceAtOffset(0x300, PointerDataType.dataType, 4, "m_pFruitTexture", null);
        s.replaceAtOffset(0x304, PointerDataType.dataType, 4, "m_pFruitTexture2", null);
        // Score fields
        s.replaceAtOffset(0x308, IntegerDataType.dataType, 4, "m_IntField308", null);
        s.replaceAtOffset(0x30c, IntegerDataType.dataType, 4, "m_field30c", null);
        s.replaceAtOffset(0x310, IntegerDataType.dataType, 4, "m_field310", null);
        s.replaceAtOffset(0x314, IntegerDataType.dataType, 4, "m_BaseScore", null);
        s.replaceAtOffset(0x318, ByteDataType.dataType, 1, "m_bScorable", null);
        s.replaceAtOffset(0x319, ByteDataType.dataType, 1, "m_bSpecial", null);
        // Impact sounds
        s.replaceAtOffset(0x31c, PointerDataType.dataType, 4, "m_pSounds", null);
        s.replaceAtOffset(0x320, IntegerDataType.dataType, 4, "m_SoundCount", null);
        // Random bonus
        s.replaceAtOffset(0x324, IntegerDataType.dataType, 4, "m_RandBonusBase", null);
        s.replaceAtOffset(0x328, IntegerDataType.dataType, 4, "m_RandBonusMax", null);
        // Power-ups
        s.replaceAtOffset(0x32c, PointerDataType.dataType, 4, "m_pPowers", null);
        commit(s);
    }

    // ===================== AUDIO =====================

    private void createMAMAudioThread() {
        StructureDataType s = make("MAMAudioThread", 0x154);
        s.replaceAtOffset(0x00, PointerDataType.dataType, 4, "vtable", null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType, 1, "m_bFieldC", null);
        s.replaceAtOffset(0x0d, ByteDataType.dataType, 1, "m_bFieldD", null);
        s.replaceAtOffset(0x10, FloatDataType.dataType, 4, "masterVolume", null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType, 4, "sampleRate", null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType, 4, "bufferSize", null);
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "m_field20", null);
        s.replaceAtOffset(0x30, IntegerDataType.dataType, 4, "voiceCount", null);
        commit(s);
    }
}
