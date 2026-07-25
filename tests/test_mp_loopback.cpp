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
//                      NOTE: Host()/Join() also queue the MP_EVT_CONNECTED/
//                      MP_EVT_NAMES session-setup events (see IMpTransport.h);
//                      the first Update() call in this test drains those too,
//                      exercising HandleP2PConnected/HandleP2PNames
//                      (P2PMessageHandling.cpp) as a side effect.
//   4. SEED        -- two independently-seeded Math::Random instances (same
//                      seed = same type WaveManager::m_Random uses) produce
//                      identical sequences -- the core parity guarantee behind
//                      StartGamePacket::m_GameSeed.
//   5. HANDSHAKE   -- WaveManager's iOS-1.5 soft handshake: SendWaveSyncPacket
//                      sets the local-sent/remote-pending flags and bumps
//                      m_SyncCounter (no hard barrier -- see WaveManager.h);
//                      RecievedSync records the peer's report and clears the
//                      retry timer/counter.
//   6. FULL SESSION -- drives the complete online-versus session-setup
//                      handshake end-to-end over a single LoopbackTransport
//                      pair by swapping the active transport
//                      (Mortar::SetMpTransport) between peer steps, same
//                      pattern as case 3's guest-side install: CONNECTED+NAMES
//                      on both ends, cmd1 ready handshake, then cmd2 seed+go
//                      with the guest==2 reseed gate proven both positive and
//                      negative. See the case-6 comment block below for which
//                      steps are driven organically (real code path) vs.
//                      manually constructed (receiver-only, sender side not
//                      yet auto-wired).
//   7. ASYNC CONNECT -- proves Host()/Join() are non-blocking/async per
//                      IMpTransport.h's contract: they return immediately
//                      with IsConnecting()==true, IsConnected()==false;
//                      pumping PollEvent() fewer than the delay's tick count
//                      keeps it CONNECTING; the Nth tick delivers
//                      MP_EVT_CONNECTED and flips IsConnected()==true.
//   8. CONNECT FAILURE -- proves a failed connect attempt (via the
//                      SetConnectShouldFail() injection hook) surfaces as
//                      MP_EVT_CONNECT_FAILED (never MP_EVT_CONNECTED),
//                      routes through NetworkManager::Update() to
//                      HandleDisconnection (observable: MP session flags
//                      cleared), and leaves IsConnected()==false with no
//                      session-start effects.
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
#include "game/GameWork.h"
#include "game/GameMode.h"

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

    // Player numbers: a=host=1, b=guest=2 (MP-revival: see IMpTransport.h --
    // guest==2 is load-bearing for the StartGamePacket cmd2 RNG reseed gate).
    CHECK(a->LocalPlayerNumber() == 1);
    CHECK(b->LocalPlayerNumber() == 2);

    // Not connected until Host()/Join().
    CHECK(!a->IsConnected());
    CHECK(!b->IsConnected());

    // MP-revival: Host()/Join() are async/non-blocking -- they return
    // "attempt accepted", not "connected" (see IMpTransport.h contract).
    // Right after calling them, both ends must be CONNECTING, not CONNECTED.
    CHECK(a->Host() == true);
    CHECK(b->Join("") == true);
    CHECK(a->IsConnecting());
    CHECK(b->IsConnecting());
    CHECK(!a->IsConnected());
    CHECK(!b->IsConnected());

    // Pump PollEvent() on both ends until the async connect resolves
    // (LOOPBACK_DEFAULT_CONNECT_DELAY ticks) -- mirrors a real per-frame poll
    // loop. Drain to MP_EVT_NONE each tick, same pattern NetworkManager::
    // Update uses.
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        while (a->PollEvent() != Mortar::MP_EVT_NONE) {}
        while (b->PollEvent() != Mortar::MP_EVT_NONE) {}
    }
    CHECK(!a->IsConnecting());
    CHECK(!b->IsConnecting());
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

    // StartGamePacket (type 103 / 0x67) -- MP-revival cmd/value rework
    // (see StartGamePacket.h): cmd=2 (seed+go) carries the online seed as m_Value.
    {
        StartGamePacket src(2, (int)0xABCD1234);
        CHECK(src.m_PacketType == 103);
        StartGamePacket dst;
        RoundTrip(src, dst);
        CHECK(dst.m_PacketType == 103);
        CHECK(dst.m_Cmd == 2);
        CHECK(dst.m_Value == (int)0xABCD1234);
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

    // MP-revival: connect is async now -- pump both ends' PollEvent() (via
    // Update()/direct calls) until CONNECTED before exercising data packets.
    // `a` is drained through the real NetworkManager::Update() path (which
    // also exercises HandleP2PConnected/HandleP2PNames as a side effect, same
    // as before); `b` is drained directly since it's never installed as the
    // active transport here.
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        nm->Update(1.0f / 60.0f);
        while (b->PollEvent() != Mortar::MP_EVT_NONE) {}
    }
    CHECK(a->IsConnected());
    CHECK(b->IsConnected());

    // -- PointsPacket -> SetOpponentScore --
    {
        PointsPacket pkt(1234, 0, 0, 0);
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(nm->GetOpponentScore() == 1234);
    }

    // -- FruitSlicedPacket -> SetLastPeerSlice (record path) --
    // NOTE: GlobalP2PMessageHandler's case 101 also looks up the referenced
    // fruit via ET_GetEntity and calls Fruit::CollisionResponse on it (the
    // iOS 1.5 "actually apply the slice" behavior) -- that needs a live Fruit
    // entity registered in EntityTracker, which needs a booted game
    // (ActorManager/EntityTracker spun up). Out of reach for this pure-logic
    // test; asserting only the record path (SetLastPeerSlice), which still
    // runs unconditionally before the entity lookup and is a real, currently
    // undeleted NetworkManager API (see NetworkManager.h).
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

    // -- StartGamePacket cmd2 (seed+go) -> WaveManager online seed --
    // MP-revival: the reseed only fires for the GUEST (LocalPlayerNumber()==2,
    // see IMpTransport.h) -- `a` is the HOST end (LocalPlayerNumber()==1) and
    // is currently the installed transport, so temporarily install `b` (the
    // guest end) to exercise the guest-side reseed path, then restore `a`.
    {
        WaveManager* wm = WaveManager::GetInstance();
        Mortar::SetMpTransport(b);
        StartGamePacket pkt(2, (int)0xABCD1234);
        // Send from the host (`a`) to the guest (`b`)'s inbound queue.
        {
            uint8_t buf[512];
            Mortar::ByteWriter w(buf, sizeof buf);
            pkt.Serialize(w);
            a->Send(buf, w.Written(), true);
        }
        Mortar::NetworkManager::GetInstance()->Update(1.0f / 60.0f);
        Mortar::SetMpTransport(a);
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

    // -- PlayerDisconnectGamePacket (type 104) -- NO LONGER DISPATCHED.
    // iOS 1.5 has no disconnect *packet* (a peer drop is a transport-level
    // event handled via DisconnectP2P -> HandleDisconnection, not a data
    // case); GlobalP2PMessageHandler's switch has no case 104 anymore (see
    // P2PMessageHandling.cpp). Assert it's silently ignored: the packet
    // drains off the wire (Update() doesn't hang/crash) and produces no
    // side effect (opponent score from the prior sub-case is untouched).
    {
        int scoreBefore = nm->GetOpponentScore();
        PlayerDisconnectGamePacket pkt;
        pkt.SetMessageText("bye");
        SendFromPeer(b, pkt);
        nm->Update(1.0f / 60.0f);
        CHECK(nm->GetOpponentScore() == scoreBefore);
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
// 5. SOFT HANDSHAKE -- WaveManager's iOS-1.5 wave-sync exchange.
//
// There is no hard barrier in the real design (WaveManager::UpdateNetworking
// was deleted along with m_WaveBoundaryPending -- see WaveManager.h/.cpp):
// GetNextWave's tail fires SendWaveSyncPacket fire-and-forget at each local
// wave boundary and spawning is never stalled waiting on the peer.
// SendWaveSyncPacket/RecievedSync only touch bookkeeping fields directly, so
// this test calls them directly rather than scripting fields by hand.
//
// WaveManager::GetInstance() is a single process-wide singleton shared with
// test_seam_dispatch() above (which exercises RecievedSync via the real
// dispatch path for case 102) -- baseline the fields here so this test is
// order-independent of what ran before it in the same process.
// ---------------------------------------------------------------------------
static void test_wave_sync_handshake() {
    Mortar::LoopbackTransport* a = 0;
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);
    a->Host();
    b->Join("");
    Mortar::SetMpTransport(a);

    // MP-revival: connect is async -- pump both ends to CONNECTED before
    // sending. Draining via raw PollEvent() (not NetworkManager::Update())
    // pops the queued MP_EVT_CONNECTED/MP_EVT_NAMES without dispatching them
    // through GlobalP2PMessageHandler, so this test's game_work is untouched
    // (case 6 is the one that exercises those HandleP2PConnected/
    // HandleP2PNames side effects via the real Update() dispatch path).
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        while (a->PollEvent() != Mortar::MP_EVT_NONE) {}
        while (b->PollEvent() != Mortar::MP_EVT_NONE) {}
    }
    CHECK(a->IsConnected());
    CHECK(b->IsConnected());

    WaveManager* wm = WaveManager::GetInstance();
    wm->m_SyncLocalReady = 1;
    wm->m_SyncRemotePending = 0;
    wm->m_SyncReceived = 0;
    wm->m_SyncWaveIdx = -1;
    wm->m_NetTimerA = 0.0f;
    wm->m_SyncCounter = 0;

    // Local wave boundary reached: SendWaveSyncPacket sends the packet (via
    // SendP2PPacket -> our loopback transport), advances the shared global
    // RNG, and marks "sent, awaiting peer".
    wm->SendWaveSyncPacket(3, 10.0f);
    CHECK(wm->m_SyncLocalReady == 0);
    CHECK(wm->m_SyncRemotePending == 1);
    CHECK(wm->m_SyncCounter == 1);
    CHECK(wm->m_NetTimerB == 0.0f);

    // Drain the sync packet WaveManager just sent to the peer end (b), so the
    // loopback queue doesn't leak into other tests. Its wire format is
    // covered by the packet round-trip test above.
    uint8_t drain[512];
    b->Poll(drain, sizeof drain);

    // Peer's sync arrives (RecievedSync is what GlobalP2PMessageHandler calls
    // for an inbound WaveSyncPacket -- see case 3 above for the full wire path).
    wm->RecievedSync(5, 42.0f);
    CHECK(wm->m_SyncReceived != 0);
    CHECK(wm->m_SyncWaveIdx == 5);
    CHECK(wm->m_NetTimerA == 0.0f);
    CHECK(wm->m_SyncCounter == 0);

    Mortar::SetMpTransport(0);
    delete a;
    delete b;
}

// ---------------------------------------------------------------------------
// 6. FULL SESSION HANDSHAKE -- host+guest over one LoopbackTransport pair,
//    driven end-to-end through the real dispatch path (NetworkManager::Update
//    -> GlobalP2PMessageHandler), swapping Mortar::SetMpTransport between
//    peer steps (same technique test_seam_dispatch's cmd2 sub-case uses to
//    install the guest end -- see case 3 above).
//
// MANUAL vs. ORGANIC split:
//   - CONNECTED/NAMES (step 2) and cmd1 ready (step 3): fully organic. Host()/
//     Join() queue the real transport events; RetryOnlineMultiplayerGame()
//     sends the real StartGamePacket(cmd1) via SendP2PPacket -> the installed
//     transport. Nothing is hand-constructed on the wire.
//   - cmd2 seed+go (step 4): the RECEIVE side (HandleP2PData case 103 cmd2)
//     is organic -- exercised by delivering a real, wire-serialized
//     StartGamePacket through NetworkManager::Update(). The SEND side is
//     MANUAL: there is no implemented "host broadcasts cmd2 once both peers
//     are ready" trigger yet (RetryOnlineMultiplayerGame only sends cmd1 --
//     see P2PMessageHandling.cpp; nothing currently calls
//     SendP2PPacket(StartGamePacket(2, seed))). This test constructs and
//     sends the cmd2 packet directly from the host's transport end to prove
//     the guest-side reseed handler, without asserting an auto-send sequence
//     that doesn't exist in the impl yet.
// ---------------------------------------------------------------------------
static void test_full_session_handshake() {
    Mortar::LoopbackTransport* host = 0;
    Mortar::LoopbackTransport* guest = 0;
    Mortar::LoopbackTransport::CreatePair(host, guest);

    // MP-revival: Host()/Join() are async/non-blocking -- return value means
    // "attempt accepted", not "connected" (see IMpTransport.h contract).
    CHECK(host->Host() == true);
    CHECK(guest->Join("") == true);
    CHECK(host->LocalPlayerNumber() == 1);
    CHECK(guest->LocalPlayerNumber() == 2);
    CHECK(host->IsConnecting());
    CHECK(guest->IsConnecting());
    CHECK(!host->IsConnected());
    CHECK(!guest->IsConnected());

    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();

    // -- Step 2: CONNECT + NAMES. Async now: pump NetworkManager::Update()
    // (installed transport alternating host/guest) LOOPBACK_DEFAULT_CONNECT_
    // DELAY times per end so the connect attempt actually resolves before
    // MP_EVT_CONNECTED/MP_EVT_NAMES are queued and drained (organic --
    // NetworkManager::Update -> PollEvent -> HandleP2PConnected/
    // HandleP2PNames). Observable effect for HandleP2PConnected: sets
    // game_work.gameMode = GAME_MODE_COMBO (the online-versus mode) and
    // game_work.m_bMPRetryPending = 1 (session-active gate) -- see
    // P2PMessageHandling.cpp. Baseline both fields first since game_work is
    // a process-wide global that earlier cases may have touched.
    game_work.gameMode = 0;
    game_work.m_bMPRetryPending = 0;

    Mortar::SetMpTransport(host);
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        nm->Update(1.0f / 60.0f);
    }
    CHECK(host->IsConnected());
    CHECK(!host->IsConnecting());
    CHECK(game_work.gameMode == GAME_MODE_COMBO);
    CHECK(game_work.m_bMPRetryPending == 1);

    // Guest end: same async CONNECTED/NAMES drain, same observable effect.
    // (Guest's connect attempt already ran its delay ticks alongside host's
    // above via the shared channel resolution gate in StartConnect/
    // PollEvent, but PollEvent must still be called on the guest's own queue
    // to drain+dispatch its events -- NetworkManager::Update does that here.)
    game_work.gameMode = 0;
    game_work.m_bMPRetryPending = 0;
    Mortar::SetMpTransport(guest);
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        nm->Update(1.0f / 60.0f);
    }
    CHECK(guest->IsConnected());
    CHECK(!guest->IsConnecting());
    CHECK(game_work.gameMode == GAME_MODE_COMBO);
    CHECK(game_work.m_bMPRetryPending == 1);

    // -- Step 3: cmd1 ready handshake, organic.
    // RetryOnlineMultiplayerGame() (called as the host) sets m_bMPRetryPending
    // = 1 (already 1 from step 2) and m_bP2PReady = 0, then sends a real
    // StartGamePacket(cmd=1) to the peer via SendP2PPacket -> the currently
    // installed transport (host).
    game_work.m_bP2PReady = 1; // baseline non-zero so the "0" assert below is meaningful
    Mortar::SetMpTransport(host);
    RetryOnlineMultiplayerGame();
    CHECK(game_work.m_bMPRetryPending == 1);
    CHECK(game_work.m_bP2PReady == 0);
    // Peer not yet ready (guest hasn't sent anything) -- ready-timeout armed.
    CHECK(game_work.m_P2PReadyTimeout == 12.0f);

    // Guest drains its inbound queue -> receives cmd1 -> HandleP2PData case
    // 103 cmd1: guest's own m_bP2PReady is 0 (never set on the guest), so the
    // "we haven't sent ours yet" branch runs and records the peer (host) as
    // ready.
    game_work.m_bP2PPeerReady = 0;
    Mortar::SetMpTransport(guest);
    nm->Update(1.0f / 60.0f);
    CHECK(game_work.m_bP2PPeerReady == 1);

    // -- Step 4: cmd2 seed+go -- the KEY assertion.
    // MANUAL: construct + send the packet directly from the host's transport
    // end (no organic "host auto-broadcasts cmd2" call site exists yet --
    // see the case-6 header comment above).
    const uint32_t SEED = 0xABCD1234u;
    {
        StartGamePacket pkt(2, (int32_t)SEED);
        uint8_t buf[512];
        Mortar::ByteWriter w(buf, sizeof buf);
        pkt.Serialize(w);
        host->Send(buf, w.Written(), true); // lands in guest's inbound queue
    }

    // Receive as the GUEST (LocalPlayerNumber()==2) -- organic dispatch:
    // NetworkManager::Update -> GlobalP2PMessageHandler -> HandleP2PData case
    // 103 cmd2 -> WaveManager::SetOnlineSeed (gated on GetLocalPlayerNumber()==2)
    // + the "GO" flags.
    WaveManager* wm = WaveManager::GetInstance();
    wm->m_HaveOnlineSeed = false;
    wm->m_OnlineSeed = 0;
    game_work.m_bP2POpponentReady = 0;
    Mortar::SetMpTransport(guest);
    nm->Update(1.0f / 60.0f);
    CHECK(wm->m_HaveOnlineSeed == true);
    CHECK(wm->m_OnlineSeed == SEED);
    // "GO" latch: game_work.m_bP2POpponentReady is the WaveManager-visible
    // checkpoint cmd2's handler always sets (see P2PMessageHandling.cpp's
    // cmd2 case); m_TutorialControl's MultiplayerTutorialControl::m_bGo is
    // the other half but is only reachable if a MultiplayerTutorialControl
    // was installed with a live HUD (game_work.mHud is null in this
    // pure-logic test, so CreateMultiplayerTutorialControl's AddControl call
    // is skipped, but the control itself IS still allocated into
    // m_TutorialControl by HandleP2PConnected in step 2 above -- so m_bGo is
    // reachable too).
    CHECK(game_work.m_bP2POpponentReady == 1);
    CHECK(game_work.m_TutorialControl != 0);
    if (game_work.m_TutorialControl != 0) {
        CHECK(static_cast<MultiplayerTutorialControl*>(game_work.m_TutorialControl)->m_bGo == true);
    }

    // -- Negative: as the HOST (LocalPlayerNumber()==1), delivering cmd2 must
    // NOT reseed -- the host keeps its own seed (guest==2 gate in
    // HandleP2PData case 103 cmd2). Clear m_HaveOnlineSeed, deliver the same
    // cmd2 packet to the host's inbound queue, confirm it stays false.
    wm->m_HaveOnlineSeed = false;
    wm->m_OnlineSeed = 0;
    {
        StartGamePacket pkt(2, (int32_t)SEED);
        uint8_t buf[512];
        Mortar::ByteWriter w(buf, sizeof buf);
        pkt.Serialize(w);
        guest->Send(buf, w.Written(), true); // lands in host's inbound queue
    }
    Mortar::SetMpTransport(host);
    nm->Update(1.0f / 60.0f);
    CHECK(wm->m_HaveOnlineSeed == false); // host does NOT reseed from its own broadcast
    // The "GO" flag itself is cmd-unconditional (set for either LocalPlayerNumber),
    // so it still flips here -- only the WaveManager reseed is guest-gated.
    CHECK(game_work.m_bP2POpponentReady == 1);

    // -- Step 5: reset so other cases are unaffected.
    Mortar::SetMpTransport(0);
    delete host;
    delete guest;
}

// ---------------------------------------------------------------------------
// 7. ASYNC CONNECT -- Host()/Join() are non-blocking: return "attempt
//    accepted" immediately (IsConnecting()==true, IsConnected()==false), and
//    resolve to MP_EVT_CONNECTED only after LOOPBACK_DEFAULT_CONNECT_DELAY
//    PollEvent() ticks on both ends (proves the async contract in
//    IMpTransport.h end-to-end, independent of case 1's basic coverage).
// ---------------------------------------------------------------------------
static void test_async_connect() {
    Mortar::LoopbackTransport* a = 0;
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);

    const int N = Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY;
    CHECK(N > 1); // otherwise the "still connecting" mid-loop assertion below is vacuous

    // Host()/Join() return immediately -- non-blocking, "accepted" not "connected".
    CHECK(a->Host() == true);
    CHECK(b->Join("") == true);
    CHECK(a->IsConnecting());
    CHECK(b->IsConnecting());
    CHECK(!a->IsConnected());
    CHECK(!b->IsConnected());

    // Pump N-1 ticks: still connecting, never jumps to CONNECTED early.
    for (int i = 0; i < N - 1; ++i) {
        CHECK(a->PollEvent() == Mortar::MP_EVT_NONE);
        CHECK(b->PollEvent() == Mortar::MP_EVT_NONE);
    }
    CHECK(a->IsConnecting());
    CHECK(b->IsConnecting());
    CHECK(!a->IsConnected());
    CHECK(!b->IsConnected());

    // Final tick: MP_EVT_CONNECTED (then MP_EVT_NAMES) delivered, state flips.
    CHECK(a->PollEvent() == Mortar::MP_EVT_CONNECTED);
    CHECK(b->PollEvent() == Mortar::MP_EVT_CONNECTED);
    CHECK(!a->IsConnecting());
    CHECK(!b->IsConnecting());
    CHECK(a->IsConnected());
    CHECK(b->IsConnected());
    CHECK(a->PollEvent() == Mortar::MP_EVT_NAMES);
    CHECK(b->PollEvent() == Mortar::MP_EVT_NAMES);
    CHECK(a->PollEvent() == Mortar::MP_EVT_NONE);
    CHECK(b->PollEvent() == Mortar::MP_EVT_NONE);

    delete a;
    delete b;
}

// ---------------------------------------------------------------------------
// 8. CONNECT FAILURE -- SetConnectShouldFail() injection resolves a connect
//    attempt to MP_EVT_CONNECT_FAILED instead of MP_EVT_CONNECTED. Driven
//    through the real NetworkManager::Update() dispatch to prove the failure
//    routes to HandleDisconnection (observable via cleared MP session flags),
//    IsConnected() stays false, and no session-start (HandleP2PConnected)
//    effects run.
// ---------------------------------------------------------------------------
static void test_connect_failure() {
    Mortar::LoopbackTransport* a = 0; // will fail to connect
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);

    a->SetConnectShouldFail(2); // 2 = timeout (see HandleDisconnection's code map)

    CHECK(a->Host() == true);
    CHECK(b->Join("") == true);
    CHECK(a->IsConnecting());
    CHECK(!a->IsConnected());

    // Baseline game_work MP flags to nonzero/sentinel so HandleDisconnection's
    // clears are observable, and gameMode so we can prove HandleP2PConnected
    // (session-start) never ran.
    game_work.m_bP2PReady         = 1;
    game_work.m_bP2PConnecting    = 1;
    game_work.m_bP2POpponentReady = 1;
    game_work.m_bMPRetryPending   = 1;
    game_work.m_bP2PPeerReady     = 1;
    game_work.m_P2PReadyTimeout   = 12.0f;
    game_work.gameMode            = 0;

    Mortar::SetMpTransport(a);
    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();

    // Pump exactly LOOPBACK_DEFAULT_CONNECT_DELAY ticks: still connecting/no
    // effects until the last one, same async timing as a successful connect
    // (case 7) -- only the outcome differs.
    const int N = Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY;
    for (int i = 0; i < N - 1; ++i) {
        nm->Update(1.0f / 60.0f);
        CHECK(a->IsConnecting());
        CHECK(!a->IsConnected());
    }
    CHECK(game_work.m_bMPRetryPending == 1); // HandleP2PConnected has NOT run yet
    CHECK(game_work.gameMode == 0);

    // Final tick: MP_EVT_CONNECT_FAILED drains through NetworkManager::Update
    // -> HandleDisconnection. b's peer end never sees CONNECTED either (the
    // channel only resolves once, to FAILED, for a's side; b's own attempt is
    // untouched by a's injected failure and would resolve independently --
    // not exercised here since this test only cares about a's outcome).
    nm->Update(1.0f / 60.0f);

    CHECK(!a->IsConnecting());
    CHECK(!a->IsConnected());

    // HandleDisconnection's clears (P2PMessageHandling.cpp): all MP session
    // flags reset to 0/0.0f.
    CHECK(game_work.m_bP2PReady == 0);
    CHECK(game_work.m_bP2PConnecting == 0);
    CHECK(game_work.m_bP2POpponentReady == 0);
    CHECK(game_work.m_bMPRetryPending == 0);
    CHECK(game_work.m_bP2PPeerReady == 0);
    CHECK(game_work.m_P2PReadyTimeout == 0.0f);

    // No session-start: HandleP2PConnected's gameMode assignment never ran.
    CHECK(game_work.gameMode == 0);

    // No stray data pump: Poll() must stay a no-op post-failure (never connected).
    uint8_t buf[64];
    CHECK(a->Poll(buf, sizeof buf) == 0);

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
    test_wave_sync_handshake();
    std::printf("  [5] wave-sync soft handshake: %s\n", g_failures == before5 ? "OK" : "FAIL");

    int before6 = g_failures;
    test_full_session_handshake();
    std::printf("  [6] full session handshake (connect/names/ready/seed): %s\n", g_failures == before6 ? "OK" : "FAIL");

    int before7 = g_failures;
    test_async_connect();
    std::printf("  [7] async connect (non-blocking Host/Join, delayed CONNECTED): %s\n", g_failures == before7 ? "OK" : "FAIL");

    int before8 = g_failures;
    test_connect_failure();
    std::printf("  [8] connect failure (MP_EVT_CONNECT_FAILED -> HandleDisconnection): %s\n", g_failures == before8 ? "OK" : "FAIL");

    if (g_failures != 0) {
        std::printf("test_mp_loopback: FAIL (%d assertion(s) failed)\n", g_failures);
        return 1;
    }
    std::printf("test_mp_loopback: PASS\n");
    return 0;
}
