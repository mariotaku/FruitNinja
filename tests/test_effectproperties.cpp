// EffectPropertyValues / EffectPropertyList / GetColourRGB unit test.
// Builds a 9-def material property set (matching the standard mesh material
// used by LoadMesh), exercises SetValue for each type bucket, reads back the
// stored bytes, and asserts correctness.
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "asset/SharedEffectProperties.h"
#include "asset/EffectDataTypes.h"
#include "math/_Vector3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_FLOAT(a, b) \
    do { \
        float _a = (a); float _b = (b); \
        if (std::fabs(_a - _b) > 1e-6f) { \
            std::printf("FAIL (%s:%d): %f != %f\n", __FILE__, __LINE__, (double)_a, (double)_b); \
            ::exit(1); \
        } \
    } while(0)

using namespace Mortar;

// The 9-def standard material property set used by mesh materials.
// Mirrors the definition list expected by LoadMesh (#118b-2).
static EffectPropertyDefinition s_Defs[] = {
    { Immutable("DiffuseMap"),       EffectDataTypes::Type_Texture2D, 1 },
    { Immutable("UVWOffset"),        EffectDataTypes::Type_Vec3,      3 },
    { Immutable("Alpha"),            EffectDataTypes::Type_Float,     1 },
    { Immutable("Ambience"),         EffectDataTypes::Type_Vec3,      1 },
    { Immutable("Diffuse"),          EffectDataTypes::Type_Vec3,      1 },
    { Immutable("SelfIllum"),        EffectDataTypes::Type_Vec3,      1 },
    { Immutable("Specular"),         EffectDataTypes::Type_Vec3,      1 },
    { Immutable("SpecularStrength"), EffectDataTypes::Type_Float,     1 },
    { Immutable("IsLit"),            EffectDataTypes::Type_Bool,      1 },
};
static const int kNumDefs = 9;

static void test_GetColourRGB()
{
    // 0x00112233: R=0x33, G=0x22, B=0x11 (byte order: ABGR packed low-to-high).
    _Vector3<float> c = GetColourRGB(0x00112233u);
    CHECK_FLOAT(c.x, 0x33 / 255.0f);
    CHECK_FLOAT(c.y, 0x22 / 255.0f);
    CHECK_FLOAT(c.z, 0x11 / 255.0f);

    // Pure-white (0x00ffffff) -> (1,1,1).
    _Vector3<float> w = GetColourRGB(0x00ffffffu);
    CHECK_FLOAT(w.x, 1.0f);
    CHECK_FLOAT(w.y, 1.0f);
    CHECK_FLOAT(w.z, 1.0f);

    // Pure-black (0x00000000) -> (0,0,0).
    _Vector3<float> bk = GetColourRGB(0x00000000u);
    CHECK_FLOAT(bk.x, 0.0f);
    CHECK_FLOAT(bk.y, 0.0f);
    CHECK_FLOAT(bk.z, 0.0f);

    // Alpha channel must be ignored: 0xff112233 same result as 0x00112233.
    _Vector3<float> ca = GetColourRGB(0xff112233u);
    CHECK_FLOAT(ca.x, 0x33 / 255.0f);
    CHECK_FLOAT(ca.y, 0x22 / 255.0f);
    CHECK_FLOAT(ca.z, 0x11 / 255.0f);
}

static void test_EffectPropertyValues_layout()
{
    // Build a values arena from the 9-def set.
    unsigned long counts[EffectDataTypes::kNumTypes];
    for (int i = 0; i < EffectDataTypes::kNumTypes; ++i) counts[i] = 0;
    for (int i = 0; i < kNumDefs; ++i) {
        counts[s_Defs[i].m_Type] += s_Defs[i].m_Count;
    }
    // Expected type counts from the 9-def set:
    //   Type_Float     (1): Alpha(1) + SpecularStrength(1) = 2
    //   Type_Bool      (2): IsLit(1)                       = 1
    //   Type_Vec3      (5): UVWOffset(3)+Ambience+Diffuse+SelfIllum+Specular = 7
    //   Type_Texture2D (7): DiffuseMap(1)                  = 1
    CHECK(counts[EffectDataTypes::Type_Float]     == 2);
    CHECK(counts[EffectDataTypes::Type_Bool]      == 1);
    CHECK(counts[EffectDataTypes::Type_Vec3]      == 7);
    CHECK(counts[EffectDataTypes::Type_Texture2D] == 1);
    // All other types should be 0.
    CHECK(counts[EffectDataTypes::Type_Int]        == 0);
    CHECK(counts[EffectDataTypes::Type_Matrix44]   == 0);
    CHECK(counts[EffectDataTypes::Type_Vec2]       == 0);
    CHECK(counts[EffectDataTypes::Type_Vec4]       == 0);
    CHECK(counts[EffectDataTypes::Type_Texture3D]  == 0);
    CHECK(counts[EffectDataTypes::Type_TextureCube]== 0);

    EffectPropertyValues vals(counts);

    // TrySetValue float at offset 0 (Alpha) and 1 (SpecularStrength).
    CHECK(vals.TrySetValue<float>(EffectDataTypes::Type_Float, 0, 0.75f));
    CHECK(vals.TrySetValue<float>(EffectDataTypes::Type_Float, 1, 0.5f));

    // Out-of-bounds offset must fail.
    CHECK(!vals.TrySetValue<float>(EffectDataTypes::Type_Float, 2, 0.0f));

    // TryGetValue reads back what we set.
    float f0 = 0.0f, f1 = 0.0f;
    CHECK(vals.TryGetValue<float>(EffectDataTypes::Type_Float, 0, f0));
    CHECK(vals.TryGetValue<float>(EffectDataTypes::Type_Float, 1, f1));
    CHECK_FLOAT(f0, 0.75f);
    CHECK_FLOAT(f1, 0.5f);

    // TrySetValue bool.
    CHECK(vals.TrySetValue<bool>(EffectDataTypes::Type_Bool, 0, false));
    bool b = true;
    CHECK(vals.TryGetValue<bool>(EffectDataTypes::Type_Bool, 0, b));
    CHECK(b == false);

    // TrySetValue Vec3 at UVWOffset offsets 0..2.
    _Vector3<float> uv0(1.0f, 2.0f, 3.0f);
    _Vector3<float> uv1(4.0f, 5.0f, 6.0f);
    _Vector3<float> uv2(7.0f, 8.0f, 9.0f);
    CHECK(vals.TrySetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 0, uv0));
    CHECK(vals.TrySetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 1, uv1));
    CHECK(vals.TrySetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 2, uv2));

    _Vector3<float> rv0, rv1, rv2;
    CHECK(vals.TryGetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 0, rv0));
    CHECK(vals.TryGetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 1, rv1));
    CHECK(vals.TryGetValue<_Vector3<float>>(EffectDataTypes::Type_Vec3, 2, rv2));
    CHECK(rv0 == uv0);
    CHECK(rv1 == uv1);
    CHECK(rv2 == uv2);

    // Type mismatch must fail.
    CHECK(!vals.TrySetValue<float>(EffectDataTypes::Type_Vec3, 0, 0.0f));

    // TrySetValue Texture2D.
    EffectTexture2D tex;
    tex.id = 42u;
    CHECK(vals.TrySetValue<EffectTexture2D>(EffectDataTypes::Type_Texture2D, 0, tex));
    EffectTexture2D rtex;
    rtex.id = 0;
    CHECK(vals.TryGetValue<EffectTexture2D>(EffectDataTypes::Type_Texture2D, 0, rtex));
    CHECK(rtex.id == 42u);

    // GetValueRef sanity.
    const float* pf = vals.GetValueRef<float>(EffectDataTypes::Type_Float, 0);
    CHECK(pf != NULL);
    CHECK_FLOAT(*pf, 0.75f);
    const float* pf_oob = vals.GetValueRef<float>(EffectDataTypes::Type_Float, 99);
    CHECK(pf_oob == NULL);
}

static void test_EffectPropertyList_SetValue()
{
    // Build a SharedEffectProperties from the 9-def set with no parent.
    SmartPtr<SharedEffectProperties> sep(
        new SharedEffectProperties(s_Defs, s_Defs + kNumDefs,
                                   SmartPtr<SharedEffectProperties>()));

    EffectPropertyList& list = sep->GetList();

    // SetValue<Vec3> via EffectPropertyList.
    _Vector3<float> ambColour = GetColourRGB(0x00112233u);
    CHECK(list.SetValue<_Vector3<float>>("Ambience", ambColour));

    // GetProperty returns non-null for known key.
    EffectProperty* propAmb = list.GetProperty("Ambience");
    CHECK(propAmb != NULL);

    // Read back via TryGetValue.
    _Vector3<float> readBack;
    CHECK(propAmb->m_Owner != NULL);
    CHECK(propAmb->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propAmb->m_Offset, readBack));
    CHECK_FLOAT(readBack.x, ambColour.x);
    CHECK_FLOAT(readBack.y, ambColour.y);
    CHECK_FLOAT(readBack.z, ambColour.z);

    // SetValue<float> for SpecularStrength.
    CHECK(list.SetValue<float>("SpecularStrength", 0.5f));
    EffectProperty* propSS = list.GetProperty("SpecularStrength");
    CHECK(propSS != NULL);
    float ssRead = 0.0f;
    CHECK(propSS->m_Owner->TryGetValue<float>(
        EffectDataTypes::Type_Float, propSS->m_Offset, ssRead));
    CHECK_FLOAT(ssRead, 0.5f);

    // SetValue<bool> for IsLit.
    CHECK(list.SetValue<bool>("IsLit", false));
    EffectProperty* propLit = list.GetProperty("IsLit");
    CHECK(propLit != NULL);
    bool litRead = true;
    CHECK(propLit->m_Owner->TryGetValue<bool>(
        EffectDataTypes::Type_Bool, propLit->m_Offset, litRead));
    CHECK(litRead == false);

    // SetValue on unknown key must return false.
    CHECK(!list.SetValue<float>("NoSuchProperty", 1.0f));

    // GetProperty on unknown key must return null.
    CHECK(list.GetProperty("NoSuchProperty") == NULL);

    // SetValue<Vec3> for Diffuse using GetColourRGB.
    _Vector3<float> diffColour = GetColourRGB(0x00aabbccu);
    CHECK(list.SetValue<_Vector3<float>>("Diffuse", diffColour));
    EffectProperty* propDiff = list.GetProperty("Diffuse");
    CHECK(propDiff != NULL);
    _Vector3<float> diffRead;
    CHECK(propDiff->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propDiff->m_Offset, diffRead));
    CHECK_FLOAT(diffRead.x, diffColour.x);
    CHECK_FLOAT(diffRead.y, diffColour.y);
    CHECK_FLOAT(diffRead.z, diffColour.z);
}

static void test_EffectProperty_SetValue_template()
{
    // Exercise EffectProperty::SetValue<T> directly.
    SmartPtr<SharedEffectProperties> sep(
        new SharedEffectProperties(s_Defs, s_Defs + kNumDefs,
                                   SmartPtr<SharedEffectProperties>()));
    EffectPropertyList& list = sep->GetList();

    EffectProperty* propAlpha = list.GetProperty("Alpha");
    CHECK(propAlpha != NULL);
    CHECK(propAlpha->SetValue<float>(0.8f));

    float alphaRead = 0.0f;
    CHECK(propAlpha->m_Owner->TryGetValue<float>(
        EffectDataTypes::Type_Float, propAlpha->m_Offset, alphaRead));
    CHECK_FLOAT(alphaRead, 0.8f);

    // Type mismatch on EffectProperty::SetValue must fail.
    CHECK(!propAlpha->SetValue<_Vector3<float>>(_Vector3<float>(1.0f, 1.0f, 1.0f)));
}

static void test_UVWOffset_multi_count()
{
    // UVWOffset has count=3; verify all 3 slots can be written independently.
    SmartPtr<SharedEffectProperties> sep(
        new SharedEffectProperties(s_Defs, s_Defs + kNumDefs,
                                   SmartPtr<SharedEffectProperties>()));
    EffectPropertyList& list = sep->GetList();

    EffectProperty* propUVW = list.GetProperty("UVWOffset");
    CHECK(propUVW != NULL);

    // UVWOffset occupies the first 3 slots in the Vec3 bucket.
    // m_Offset should be 0 (first Vec3 in the sorted order — but sort is alphabetical).
    // The property slot is at m_Offset; write at offset + 0, +1, +2 manually.
    _Vector3<float> uv0(0.1f, 0.2f, 0.3f);
    _Vector3<float> uv1(0.4f, 0.5f, 0.6f);
    _Vector3<float> uv2(0.7f, 0.8f, 0.9f);

    // Slots are at propUVW->m_Offset + 0, +1, +2.
    CHECK(propUVW->m_Owner->TrySetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 0, uv0));
    CHECK(propUVW->m_Owner->TrySetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 1, uv1));
    CHECK(propUVW->m_Owner->TrySetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 2, uv2));

    _Vector3<float> r0, r1, r2;
    CHECK(propUVW->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 0, r0));
    CHECK(propUVW->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 1, r1));
    CHECK(propUVW->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, propUVW->m_Offset + 2, r2));
    CHECK(r0 == uv0);
    CHECK(r1 == uv1);
    CHECK(r2 == uv2);
}

static void test_parent_chain()
{
    // Build a parent SharedEffectProperties with a subset of defs,
    // then a child that adds the rest. GetProperty should find parent defs
    // via the recursive parent-chain lookup.
    static EffectPropertyDefinition s_ParentDefs[] = {
        { Immutable("Diffuse"), EffectDataTypes::Type_Vec3, 1 },
        { Immutable("IsLit"),   EffectDataTypes::Type_Bool, 1 },
    };
    static EffectPropertyDefinition s_ChildDefs[] = {
        { Immutable("Diffuse"), EffectDataTypes::Type_Vec3, 1 },  // already in parent
        { Immutable("Alpha"),   EffectDataTypes::Type_Float, 1 }, // new in child
    };

    SmartPtr<SharedEffectProperties> parent(
        new SharedEffectProperties(s_ParentDefs, s_ParentDefs + 2,
                                   SmartPtr<SharedEffectProperties>()));
    SmartPtr<SharedEffectProperties> child(
        new SharedEffectProperties(s_ChildDefs, s_ChildDefs + 2, parent));

    EffectPropertyList& pList = parent->GetList();
    EffectPropertyList& cList = child->GetList();

    // Parent has Diffuse + IsLit.
    CHECK(pList.GetProperty("Diffuse") != NULL);
    CHECK(pList.GetProperty("IsLit")   != NULL);
    CHECK(pList.GetProperty("Alpha")   == NULL);

    // Child has Alpha; Diffuse already in parent so not duplicated in child.
    // GetProperty on child for Diffuse must walk up to parent.
    EffectProperty* cDiffuse = cList.GetProperty("Diffuse");
    CHECK(cDiffuse != NULL);

    // Alpha is in child.
    EffectProperty* cAlpha = cList.GetProperty("Alpha");
    CHECK(cAlpha != NULL);

    // IsLit is in parent; child lookup must find it via recursion.
    EffectProperty* cIsLit = cList.GetProperty("IsLit");
    CHECK(cIsLit != NULL);

    // Set Diffuse on parent and read back via the property pointer.
    _Vector3<float> diffVal(0.5f, 0.6f, 0.7f);
    CHECK(pList.SetValue<_Vector3<float>>("Diffuse", diffVal));
    _Vector3<float> readBack;
    CHECK(cDiffuse->m_Owner->TryGetValue<_Vector3<float>>(
        EffectDataTypes::Type_Vec3, cDiffuse->m_Offset, readBack));
    CHECK_FLOAT(readBack.x, 0.5f);
    CHECK_FLOAT(readBack.y, 0.6f);
    CHECK_FLOAT(readBack.z, 0.7f);
}

int main()
{
    printf("test_effectproperties: start\n");

    test_GetColourRGB();
    printf("  GetColourRGB: OK\n");

    test_EffectPropertyValues_layout();
    printf("  EffectPropertyValues layout + TrySetValue/TryGetValue: OK\n");

    test_EffectPropertyList_SetValue();
    printf("  EffectPropertyList::SetValue<T>: OK\n");

    test_EffectProperty_SetValue_template();
    printf("  EffectProperty::SetValue<T>: OK\n");

    test_UVWOffset_multi_count();
    printf("  UVWOffset multi-count (cnt=3): OK\n");

    test_parent_chain();
    printf("  parent-chain lookup: OK\n");

    printf("test_effectproperties: PASS\n");
    return 0;
}
