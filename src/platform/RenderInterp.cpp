// Port specific: render interpolation implementation.
// Entire file is compiled only when FN_RENDER_INTERP is on (non-asm-verify builds).

#include "platform/RenderInterp.h"

#if defined(FN_RENDER_INTERP) && FN_RENDER_INTERP

#include "entities/ActorManager.h"
#include "game/GameWork.h"
#include <list>
#include <cmath>

namespace fn {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static RenderInterp s_instance;

RenderInterp& RenderInterp::Get() {
    return s_instance;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Shortest-arc slerp.  Negates b if dot < 0 so the interpolation takes the
// short path around the unit sphere.
Quaternion RenderInterp::Slerp(const Quaternion& a, const Quaternion& b_in, float t) {
    Quaternion b = b_in;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        // Linear fallback when nearly identical.
        Quaternion r(a.x + (b.x - a.x) * t,
                     a.y + (b.y - a.y) * t,
                     a.z + (b.z - a.z) * t,
                     a.w + (b.w - a.w) * t);
        return r.normalized();
    }
    float theta0 = acosf(dot);
    float theta  = theta0 * t;
    float sinT0  = sinf(theta0);
    float sinT   = sinf(theta);
    float sa = cosf(theta) - dot * (sinT / sinT0);
    float sb = sinT / sinT0;
    return Quaternion(a.x * sa + b.x * sb,
                      a.y * sa + b.y * sb,
                      a.z * sa + b.z * sb,
                      a.w * sa + b.w * sb);
}

// ---------------------------------------------------------------------------
// Capture helpers
// ---------------------------------------------------------------------------

void RenderInterp::CaptureFruit(Mortar::Entity* e, EntitySnap& d) {
    Fruit* f     = static_cast<Fruit*>(e);
    d.pos        = f->pos;
    d.scale      = f->scale;
    d.zPosFruit  = f->m_ZPosition;
    d.secondPos  = f->m_SecondPos;
    d.rot1       = f->m_Rot1;
    d.rot2       = f->m_Rot2;
    d.runtimeId  = f->m_RuntimeID;
    d.entityType = 0;
}

void RenderInterp::CaptureBomb(Mortar::Entity* e, EntitySnap& d) {
    Bomb* b      = static_cast<Bomb*>(e);
    d.pos        = b->pos;
    d.scale      = b->scale;
    d.zPosBomb   = b->m_ZPosition;
    d.rotX       = b->m_RotX;
    d.rotY       = b->m_RotY;
    d.runtimeId  = b->m_RuntimeID;
    d.entityType = 1;
}

void RenderInterp::CaptureJiblet(Mortar::Entity* e, EntitySnap& d) {
    d.pos        = e->pos;
    d.scale      = e->scale;
    d.runtimeId  = e->m_RuntimeID;
    d.entityType = 5;
}

// ---------------------------------------------------------------------------
// SnapshotAfterStep
// ---------------------------------------------------------------------------

void RenderInterp::SnapshotAfterStep() {
    // Port specific: #172 -- UI screens (ShopScreen, frontend) place real Fruit
    // entities in ActorManager type-0 for button decorations; interpolating them
    // causes flicker.  Gating Snapshot here also prevents stale frontend snapshots
    // leaking into gameplay on re-entry (first step's hadPrev will be false -> no
    // teleport-lerp).
    if (game_work.taskStateIndex != 2) return;

    m_prev = m_cur;   // rotate: cur becomes prev
    m_cur.clear();

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am || !am->m_pTypeLists) return;

    // Type 0: Fruit
    if (0 < am->m_NumTypes) {
        const std::list<Mortar::Entity*>& fruits = am->GetTypeList(0);
        for (std::list<Mortar::Entity*>::const_iterator it = fruits.begin();
             it != fruits.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e) continue;
            if ((e->flags & ENT_SKIP_MASK) != 0) continue;
            EntitySnap snap;
            CaptureFruit(e, snap);
            // hadPrev = prev contains this pointer with same runtimeId and type
            std::unordered_map<Mortar::Entity*, EntitySnap>::iterator pit = m_prev.find(e);
            snap.hadPrev = (pit != m_prev.end() &&
                            pit->second.runtimeId  == snap.runtimeId &&
                            pit->second.entityType == snap.entityType);
            m_cur[e] = snap;
        }
    }

    // Type 1: Bomb
    if (1 < am->m_NumTypes) {
        const std::list<Mortar::Entity*>& bombs = am->GetTypeList(1);
        for (std::list<Mortar::Entity*>::const_iterator it = bombs.begin();
             it != bombs.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e) continue;
            if ((e->flags & ENT_SKIP_MASK) != 0) continue;
            EntitySnap snap;
            CaptureBomb(e, snap);
            std::unordered_map<Mortar::Entity*, EntitySnap>::iterator pit = m_prev.find(e);
            snap.hadPrev = (pit != m_prev.end() &&
                            pit->second.runtimeId  == snap.runtimeId &&
                            pit->second.entityType == snap.entityType);
            m_cur[e] = snap;
        }
    }

    // Type 5: Jiblet (only valid when ActorManager has >= 6 types)
    if (5 < am->m_NumTypes) {
        const std::list<Mortar::Entity*>& jiblets = am->GetTypeList(5);
        for (std::list<Mortar::Entity*>::const_iterator it = jiblets.begin();
             it != jiblets.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e) continue;
            if ((e->flags & ENT_SKIP_MASK) != 0) continue;
            EntitySnap snap;
            CaptureJiblet(e, snap);
            std::unordered_map<Mortar::Entity*, EntitySnap>::iterator pit = m_prev.find(e);
            snap.hadPrev = (pit != m_prev.end() &&
                            pit->second.runtimeId  == snap.runtimeId &&
                            pit->second.entityType == snap.entityType);
            m_cur[e] = snap;
        }
    }
}

// ---------------------------------------------------------------------------
// Apply helpers
// ---------------------------------------------------------------------------

void RenderInterp::ApplyFruit(Mortar::Entity* e,
                               const EntitySnap& prev, const EntitySnap& cur, float alpha) {
    Fruit* f       = static_cast<Fruit*>(e);
    f->pos         = LerpVec3(prev.pos,       cur.pos,       alpha);
    f->scale       = LerpVec3(prev.scale,     cur.scale,     alpha);
    f->m_ZPosition = Lerpf(prev.zPosFruit,    cur.zPosFruit, alpha);
    f->m_SecondPos = LerpVec3(prev.secondPos, cur.secondPos, alpha);
    f->m_Rot1      = Slerp(prev.rot1, cur.rot1, alpha);
    f->m_Rot2      = Slerp(prev.rot2, cur.rot2, alpha);
}

void RenderInterp::ApplyBomb(Mortar::Entity* e,
                              const EntitySnap& prev, const EntitySnap& cur, float alpha) {
    Bomb* b       = static_cast<Bomb*>(e);
    b->pos        = LerpVec3(prev.pos,   cur.pos,   alpha);
    b->scale      = LerpVec3(prev.scale, cur.scale, alpha);
    b->m_ZPosition = Lerpf(prev.zPosBomb, cur.zPosBomb, alpha);

    // Wrap-aware int16 lerp: delta cast to int16_t handles wrap correctly.
    int16_t dX = static_cast<int16_t>(cur.rotX - prev.rotX);
    int16_t dY = static_cast<int16_t>(cur.rotY - prev.rotY);
    b->m_RotX = static_cast<int16_t>(prev.rotX + static_cast<int16_t>(
        static_cast<int>(static_cast<float>(dX) * alpha + (alpha >= 0.0f ? 0.5f : -0.5f))));
    b->m_RotY = static_cast<int16_t>(prev.rotY + static_cast<int16_t>(
        static_cast<int>(static_cast<float>(dY) * alpha + (alpha >= 0.0f ? 0.5f : -0.5f))));
}

void RenderInterp::ApplyJiblet(Mortar::Entity* e,
                                const EntitySnap& prev, const EntitySnap& cur, float alpha) {
    e->pos = LerpVec3(prev.pos, cur.pos, alpha);
}

// ---------------------------------------------------------------------------
// Restore helpers
// ---------------------------------------------------------------------------

void RenderInterp::RestoreFruit(Mortar::Entity* e, const EntitySnap& cur) {
    Fruit* f       = static_cast<Fruit*>(e);
    f->pos         = cur.pos;
    f->scale       = cur.scale;
    f->m_ZPosition = cur.zPosFruit;
    f->m_SecondPos = cur.secondPos;
    f->m_Rot1      = cur.rot1;
    f->m_Rot2      = cur.rot2;
}

void RenderInterp::RestoreBomb(Mortar::Entity* e, const EntitySnap& cur) {
    Bomb* b        = static_cast<Bomb*>(e);
    b->pos         = cur.pos;
    b->scale       = cur.scale;
    b->m_ZPosition = cur.zPosBomb;
    b->m_RotX      = cur.rotX;
    b->m_RotY      = cur.rotY;
}

void RenderInterp::RestoreJiblet(Mortar::Entity* e, const EntitySnap& cur) {
    e->pos = cur.pos;
}

// ---------------------------------------------------------------------------
// ApplyForDraw
// ---------------------------------------------------------------------------

void RenderInterp::ApplyForDraw(float alpha) {
    if (alpha <= 0.0f) return;
    // Port specific: #172 -- gate interpolation to active gameplay only.
    if (game_work.taskStateIndex != 2) return;

    for (std::unordered_map<Mortar::Entity*, EntitySnap>::iterator it = m_cur.begin();
         it != m_cur.end(); ++it) {
        if (!it->second.hadPrev) continue;

        Mortar::Entity* e = it->first;
        if (!e) continue;
        // Re-check entity is still active (could have been killed between
        // SnapshotAfterStep and this call, though ordinarily it isn't).
        if ((e->flags & ENT_SKIP_MASK) != 0) continue;

        std::unordered_map<Mortar::Entity*, EntitySnap>::iterator pit = m_prev.find(e);
        if (pit == m_prev.end()) continue;

        const EntitySnap& prev = pit->second;
        const EntitySnap& cur  = it->second;

        switch (cur.entityType) {
        case 0: ApplyFruit (e, prev, cur, alpha); break;
        case 1: ApplyBomb  (e, prev, cur, alpha); break;
        case 5: ApplyJiblet(e, prev, cur, alpha); break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// RestoreAfterDraw
// ---------------------------------------------------------------------------

void RenderInterp::RestoreAfterDraw() {
    for (std::unordered_map<Mortar::Entity*, EntitySnap>::iterator it = m_cur.begin();
         it != m_cur.end(); ++it) {
        if (!it->second.hadPrev) continue;

        Mortar::Entity* e = it->first;
        if (!e) continue;
        if ((e->flags & ENT_SKIP_MASK) != 0) continue;

        const EntitySnap& cur = it->second;
        switch (cur.entityType) {
        case 0: RestoreFruit (e, cur); break;
        case 1: RestoreBomb  (e, cur); break;
        case 5: RestoreJiblet(e, cur); break;
        default: break;
        }
    }
}

} // namespace fn

#endif // FN_RENDER_INTERP
