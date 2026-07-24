// test_mp_loopback -- end-to-end proof that the revived online-MP plumbing
// works in one process, using LoopbackTransport as both peers' transport.
//
// Covers (see case numbering in comments below):
//   1. TRANSPORT   -- LoopbackTransport::CreatePair basics (Send/Poll,
//                      IsConnected, LocalPlayerNumber).
//   2. PACKET      -- Serialize/Deserialize round-trip for all 5 packet
//                      types, plus a PacketFactory::Create id->type regression
//                      guard (100/101/102/103/104).
//   3. SEAM        -- peer sends a wire-format packet -> our end's
//                      NetworkManager::Update() drains the transport and
//                      dispatches through GlobalP2PMessageHandler, landing in
//                      the expected NetworkManager/WaveManager side effect.
//   4. SEED        -- two independently-seeded Math::Random instances (same
//                      seed = same type WaveManager::m_Random uses) produce
//                      identical sequences -- the core parity guarantee behind
//                      StartGamePacket::m_GameSeed.
//   5. BARRIER     -- WaveManager::UpdateNetworking's per-wave gate: closed
//                      with no peer sync, opens once RecievedSync arrives.
//
// Pure in-process: no GPU, no audio, no SDL/window. Links fruit-ninja-game
// directly (same pattern as test_scrollingmenu_updaterealtime.cpp) since
// WaveManager/NetworkManager/the packet classes/PacketFactory all live there;
// none of the exercised code paths require game.init()/SDL_Init -- singletons
// are bare static instances and GetCurrentScore() null-checks Game::GetInstance().
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "engine/network/IMpTransport.h"
#include "engine/network/LoopbackTransport.h"
#include "engine/network/MpTransport.h"
#include "engine/network/ByteBuffer.h"
#include "engine/network/NetworkPacket.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "engine/math/Random.h"

#include "game/PointsPacket.h"
#include "game/FruitSlicedPacket.h"
#include "game/WaveSyncPacket.h"
#include "game/StartGamePacket.h"
#include "game/PlayerDisconnectGamePacket.h"
#include "game/PacketFactory.h"
#include "game/WaveManager.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static int g_failures = 0;

// ---------------------------------------------------------------------------
// 1. TRANSPORT basics
// ---------------------------------------------------------------------------
static void test_transport_basics() {
    Mortar::LoopbackTransport* a = 0;
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);
    CHECK(a != 0 && b != 0);

    // Player numbers: a=host=0, b=peer=1.
    CHECK(a->LocalPlayerNumber() == 0);
    CHECK(b->LocalPlayerNumber() == 1);

    // Not connected until Host()/Join().
    CHECK(!a->IsConnected());
    CHECK(!b->IsConnected());

    CHECK(a->Host() == true);
    CHECK(b->Join("") == true);
    CHECK(a->IsConnected());
    CHECK(b->IsConnected());

    // Empty Poll returns 0.
    uint8_t buf[64];
    CHECK(a->Poll(buf, sizeof buf) == 0);
    CHECK(b->Poll(buf, sizeof buf) == 0);

    // a->Send -> b->Poll returns the exact bytes.
    const uint8_t msg1[] = { 1, 2, 3, 4, 5, 0xAA, 0xBB };
    a->Send(msg1, sizeof msg1, true);
    uint8_t got1[64];
    std::memset(got1, 0, sizeof got1);
    int n1 = b->Poll(got1, sizeof got1);
    CHECK(n1 == (int)sizeof msg1);
    CHECK(std::memcmp(got1, msg1, sizeof msg1) == 0);
    // Drained -- next Poll is empty again.
    CHECK(b->Poll(buf, sizeof buf) == 0);

    // b->Send -> a->Poll returns the exact bytes.
    const uint8_t msg2[] = { 9, 8, 7, 6 };
    b->Send(msg2, sizeof msg2, false);
    uint8_t got2[64];
    std::memset(got2, 0, sizeof got2);
    int n2 = a->Poll(got2, sizeof got2);
    CHECK(n2 == (int)sizeof msg2);
    CHECK(std::memcmp(got2, msg2, sizeof msg2) == 0);
    CHECK(a->Poll(buf, sizeof buf) == 0);

    delete a;
    delete b;
}

// ---------------------------------------------------------------------------
// 2. PACKET round-trip (pure, no singletons) + PacketFactory type-id guard
// ---------------------------------------------------------------------------

template <typename T>
static void RoundTrip(const T& src, T& dst) {
    uint8_t buf[512];
    Mortar::ByteWriter w(buf, sizeof buf);
    src.Serialize(w);
    Mortar::ByteReader r(buf, w.Written());
    dst.Deserialize(r);
}

static void test_packet_roundtrip() {
    // PointsPacket (type 100)
    {
        PointsPacket src(1234, 11, 22, 33);
        CHECK(src.m_PacketType == 100);
        PointsPacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 100);
        CHECK(dst.m_PacketSize == (int)sizeof(PointsPacket));
        CHECK(dst.m_Points == 1234);
        CHECK(dst.m_reserved18 == 11);
        CHECK(dst.m_reserved1c == 22);
        CHECK(dst.m_reserved20 == 33);
    }

    // FruitSlicedPacket (type 101 / 0x65)
    {
        FruitSlicedPacket src(777, 111, 222, 1.5f, 2);
        CHECK(src.m_PacketType == 101);
        FruitSlicedPacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 101);
        CHECK(dst.m_FruitId == 777);
        CHECK(dst.m_SliceX == 111);
        CHECK(dst.m_SliceY == 222);
        CHECK(dst.m_SliceAngle == 1.5f);
        CHECK(dst.m_PlayerIdx == 2);
    }

    // WaveSyncPacket (type 102 / 0x66)
    {
        WaveSyncPacket src(5, 0, 42.0f);
        CHECK(src.m_PacketType == 102);
        WaveSyncPacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 102);
        CHECK(dst.m_WaveIdx == 5);
        CHECK(dst.m_WaveData18 == 0);
        CHECK(dst.m_Score == 42.0f);
        CHECK(dst.m_reserved20 == 0);
        CHECK(dst.m_Flag24 == 0);
    }

    // StartGamePacket (type 103 / 0x67)
    {
        StartGamePacket src((int)0xABCD1234);
        CHECK(src.m_PacketType == 103);
        StartGamePacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 103);
        CHECK(dst.m_Flags == 0x18bb8);
        CHECK(dst.m_GameSeed == (int)0xABCD1234);
    }

    // PlayerDisconnectGamePacket (type 104)
    {
        PlayerDisconnectGamePacket src;
        src.SetMessageText("peer left");
        src.m_PlayerIdx = 1;
        CHECK(src.m_PacketType == 104);
        PlayerDisconnectGamePacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 104);
        CHECK(std::strcmp(dst.m_MessageText, "peer left") == 0);
        CHECK(dst.m_PlayerIdx == 1);
    }

    // PacketFactory::Create id->type regression guard (the swap-fix guard).
    {
        Mortar::NetworkPacket peek;

        peek.m_PacketType = 100;
        Mortar::NetworkPacket* p100 = PacketFactory::Create(&peek);
        CHECK(dynamic_cast<PointsPacket*>(p100) != 0);
        delete p100;

        peek.m_PacketType = 101;
        Mortar::NetworkPacket* p101 = PacketFactory::Create(&peek);
        CHECK(dynamic_cast<FruitSlicedPacket*>(p101) != 0);
        delete p101;

        peek.m_PacketType = 102;
        Mortar::NetworkPacket* p102 = PacketFactory::Create(&peek);
        CHECK(dynamic_cast<WaveSyncPacket*>(p102) != 0);
        delete p102;

        peek.m_PacketType = 103;
        Mortar::NetworkPacket* p103 = PacketFactory::Create(&peek);
        CHECK(dynamic_cast<StartGamePacket*>(p103) != 0);
        delete p103;

        peek.m_PacketType = 104;
        Mortar::NetworkPacket* p104 = PacketFactory::Create(&peek);
        CHECK(dynamic_cast<PlayerDisconnectGamePacket*>(p104) != 0);
        delete p104;

        peek.m_PacketType = 999;
        Mortar::NetworkPacket* pNone = PacketFactory::Create(&peek);
        CHECK(pNone == 0);
    }
}

// ---------------------------------------------------------------------------
// 3. SEAM dispatch: peer sends real wire-format packets, our end's
//    NetworkManager::Update() drain routes them to the expected side effect.
// ---------------------------------------------------------------------------
static void SendFromPeer(Mortar::LoopbackTransport* peerEnd, const Mortar::NetworkPacket& pkt) {
    uint8_t buf[512];
    Mortar::ByteWriter w(buf, sizeof buf);
    pkt.Serialize(w);
    peerEnd->Send(buf, w.Written(), true);
}

static void test_seam_dispatch() {
    Mortar::LoopbackTransport* a = 0; // our end (host)
    Mortar::LoopbackTransport* b = 0; // peer end
    Mortar::LoopbackTransport::CreatePair(a, b);
    a->Host();
    b->Join("");

    // Install `a` as the active transport so NetworkManager::Update() (which
    // reads GetMpTransport()) drains our inbound queue.
    Mortar::SetMpTransport(a);

    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();

    // -- PointsPacket -> SetOpponentScore --
    {
        PointsPacket pkt(1234, 0, 0, 0);
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(nm->GetOpponentScore() == 1234);
    }

    // -- FruitSlicedPacket -> SetLastPeerSlice --
    {
        FruitSlicedPacket pkt(42, 100, 200, 0.75f, 1);
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(nm->GetLastPeerFruitId() == 42);
        CHECK(nm->GetLastPeerSliceX() == 100);
        CHECK(nm->GetLastPeerSliceY() == 200);
        CHECK(nm->GetLastPeerSliceAngle() == 0.75f);
        CHECK(nm->GetLastPeerSlicePlayerIdx() == 1);
    }

    // -- StartGamePacket -> WaveManager online seed --
    {
        WaveManager* wm = WaveManager::GetInstance();
        StartGamePacket pkt((int)0xABCD1234);
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(wm->m_HaveOnlineSeed);
        CHECK(wm->m_OnlineSeed == 0xABCD1234u);
    }

    // -- WaveSyncPacket -> WaveManager sync fields --
    {
        WaveManager* wm = WaveManager::GetInstance();
        WaveSyncPacket pkt(5, 0, 42.0f);
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(wm->m_SyncReceived != 0);
        CHECK(wm->m_SyncWaveIdx == 5);
    }

    // -- PlayerDisconnectGamePacket -> OnP2PGameOver --
    {
        PlayerDisconnectGamePacket pkt;
        pkt.SetMessageText("bye");
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(nm->OnMultiplayerDisconnect());
    }

    // Uninstall so other tests / the process default state aren't affected.
    Mortar::SetMpTransport(0);
    delete a;
    delete b;
}

// ---------------------------------------------------------------------------
// 4. SEED DETERMINISM -- two independent RNGs seeded identically produce
//    identical sequences (the parity guarantee behind StartGamePacket's seed).
//
// LIMITATION: WaveManager can't cheaply be driven through a full spawn
// decision in this pure-logic test (SpawnFruit/SpawnBomb need ActorManager /
// FruitInfo / XML-loaded WAVE_INFO, which need a full game boot -- see
// test_wave_timeline.cpp's fn::TestHarness dependency). This test therefore
// asserts RNG-stream parity directly on Math::Random (the exact type
// WaveManager::m_Random uses), which is the mechanism SetOnlineSeed +
// m_Random.Seed(m_OnlineSeed) relies on -- sufficient to prove "same seed ->
// same draw sequence"; it does not itself replay a live WaveManager spawn.
// ---------------------------------------------------------------------------
static void test_seed_determinism() {
    const uint32_t seed = 0xABCD1234u;
    Math::Random rngA(seed);
    Math::Random rngB(seed);

    const int N = 32;
    for (int i = 0; i < N; ++i) {
        uint32_t a = rngA.Rand32(1000000U);
        uint32_t b = rngB.Rand32(1000000U);
        CHECK(a == b);
    }
    // Also check RandF parity (used for e.g. spawn angle/velocity draws).
    for (int i = 0; i < N; ++i) {
        float a = rngA.RandF(360.0f);
        float b = rngB.RandF(360.0f);
        CHECK(a == b);
    }

    // Cross-check against WaveManager::GetRandom()'s actual type: seeding it
    // the same way must reproduce the same stream too (same underlying type).
    WaveManager* wm = WaveManager::GetInstance();
    wm->GetRandom().Seed(seed);
    Math::Random rngC(seed);
    for (int i = 0; i < N; ++i) {
        uint32_t a = wm->GetRandom().Rand32(1000000U);
        uint32_t c = rngC.Rand32(1000000U);
        CHECK(a == c);
    }
}

// ---------------------------------------------------------------------------
// 5. WAVE BARRIER logic -- WaveManager::UpdateNetworking's per-wave gate.
//
// Scripted setup: WaveManager::Reset() is a no-op without a booted Game
// instance (Game::GetInstance() null-guard at its top -- see WaveManager.cpp),
// so this test scripts the sync fields directly instead of calling Reset().
// It uses m_SyncLocalReady=0 (the pre-first-boundary state -- see the
// in-function comment below for why this differs from Reset()'s own
// baseline). It also scripts m_WaveBoundaryPending directly
// (the port-only edge trigger WaveManager::GetNextWave() would normally set)
// since GetNextWave needs a live wave/spawner state we can't cheaply build
// here (see WaveManager::Reset/SetupWaveQue's XML+ActorManager dependencies).
// The gate math itself (m_SyncLocalReady/m_SyncRemotePending/m_SyncReceived
// and the +0x00 HUDControl3d* suppression slot) is organic: driven through
// the real UpdateNetworking()/RecievedSync() calls, not hand-poked. Also note
// WaveManager::GetInstance() is a single process-wide singleton shared with
// test_seam_dispatch() above (which exercises the same sync fields via the
// real dispatch path) -- resetting the baseline here keeps this test
// order-independent of what ran before it in the same process.
// ---------------------------------------------------------------------------
static void test_wave_barrier() {
    Mortar::LoopbackTransport* a = 0;
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);
    a->Host();
    b->Join("");
    Mortar::SetMpTransport(a);

    WaveManager* wm = WaveManager::GetInstance();
    // Baseline the sync fields so the boundary-pending edge below actually
    // triggers UpdateNetworking's send-and-close branch (guarded by
    // `m_WaveBoundaryPending && !m_SyncLocalReady`). Reset() itself would
    // set m_SyncLocalReady=1 (see WaveManager.h's ctor TODO -- the binary
    // ctor BSS-zeroes it and only Reset() sets it to 1), which is the
    // POST-first-round steady state, not the pre-first-boundary state this
    // case needs; 0 here reproduces "local side has not yet sent its sync
    // for the upcoming wave boundary".
    wm->m_SyncLocalReady = 0;
    wm->m_SyncRemotePending = 0;
    wm->m_SyncReceived = 0;
    wm->m_SyncWaveIdx = -1;

    // Simulate the local-wave-boundary edge that GetNextWave() sets.
    // m_WaveBoundaryPending is a public port-only field (no binary counterpart --
    // see WaveManager.h); poking it directly is not a test-only hook.
    wm->m_WaveBoundaryPending = true;

    // First poll: local side sends its sync packet (via SendP2PPacket -> our
    // loopback transport) and the gate closes (no peer sync yet).
    int gate1 = wm->UpdateNetworking(1.0f / 60.0f, 0);
    CHECK(gate1 != 0); // gate closed -- spawn suppressed

    // Drain the sync packet WaveManager just sent to the peer end (b), so the
    // loopback queue doesn't leak into other tests. We don't need its
    // contents here -- SendWaveSyncPacket's own wire format is covered by
    // the packet round-trip test above.
    uint8_t drain[512];
    b->Poll(drain, sizeof drain);

    // No peer sync delivered yet -- gate stays closed on a second poll.
    int gate2 = wm->UpdateNetworking(1.0f / 60.0f, 0);
    CHECK(gate2 != 0);

    // Peer's sync arrives (RecievedSync is what GlobalP2PMessageHandler calls
    // for an inbound WaveSyncPacket -- see case 3 above for the full wire path).
    wm->RecievedSync(5, 42.0f);

    // Both sides now ready -- gate opens.
    int gate3 = wm->UpdateNetworking(1.0f / 60.0f, 0);
    CHECK(gate3 == 0);

    Mortar::SetMpTransport(0);
    delete a;
    delete b;
}

int main() {
    std::printf("test_mp_loopback: start\n");

    test_transport_basics();
    std::printf("  [1] transport basics: %s\n", g_failures == 0 ? "OK" : "FAIL");

    int before2 = g_failures;
    test_packet_roundtrip();
    std::printf("  [2] packet round-trip + factory id guard: %s\n", g_failures == before2 ? "OK" : "FAIL");

    int before3 = g_failures;
    test_seam_dispatch();
    std::printf("  [3] seam dispatch (NetworkManager::Update drain): %s\n", g_failures == before3 ? "OK" : "FAIL");

    int before4 = g_failures;
    test_seed_determinism();
    std::printf("  [4] seed determinism (RNG stream parity): %s\n", g_failures == before4 ? "OK" : "FAIL");

    int before5 = g_failures;
    test_wave_barrier();
    std::printf("  [5] wave barrier gate transition: %s\n", g_failures == before5 ? "OK" : "FAIL");

    if (g_failures != 0) {
        std::printf("test_mp_loopback: FAIL (%d assertion(s) failed)\n", g_failures);
        return 1;
    }
    std::printf("test_mp_loopback: PASS\n");
    return 0;
}
