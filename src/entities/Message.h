#ifndef FN_ENTITIES_MESSAGE_H
#define FN_ENTITIES_MESSAGE_H

// Mortar::Message and Mortar::MessageListener — POD envelope + filter record.
// Binary layout confirmed from Mortar::ActorManager::SendMessage @ 0x0016ffd8 and
// v1.6.1 Mortar::Entity::ReceiveMessage @ 0x00256274.
//
// Defunct: Mortar messaging — no-op stub; v1.6.1 binary @ 0x0016ffd8 (Send),
//   0x0017085c (Add), 0x00170124 (Remove). Listener subsystem wired but
//   never instantiated in shipped retail.
//
// Analysed: 2026-05-04T00:00

#include <cstddef>

namespace Mortar {

// Mortar::Message — 8-byte POD message envelope; binary @ no class ctor (plain POD).
// SendMessage @ 0x0016ffd8 reads msg->type at +4;
// v1.6.1 Mortar::Entity::ReceiveMessage @ 0x00256274 does the same.
struct Message {
    unsigned int  reserved0;  // +0x00 — unread by SendMessage/ReceiveMessage; opaque sender slot
    int           type;       // +0x04 — primary discriminator (filter key)
};

#ifdef __bada__
static_assert(sizeof(Message) == 8, "Message size mismatch");
static_assert(offsetof(Message, type) == 4, "Message::type offset mismatch");
#endif

// Mortar::MessageListener — 16-byte POD filter+callback record; binary @ no class ctor.
// SendMessage filter (0x0016ffd8): type is exact-match; senderId/msgKind use 0=any wildcard.
struct MessageListener {
    unsigned int   type;      // +0x00 — exact-match against Message::type (0 is a real value, not wildcard)
    int            senderId;  // +0x04 — Mortar::Entity::id (Mortar::Entity+0x04) filter; 0 = any
    unsigned int   msgKind;   // +0x08 — SendMessage's msgKind key filter; 0 = any
    void*          callback;  // +0x0C — Mortar::Delegate3<void, Mortar::Entity*, Mortar::Entity*, Message*>*
                              //         polymorphism lives here, not on MessageListener itself.
                              //         Binary calls callback->vtable[+0x30](sender, target, msg).
};

// Binary is ARM32 (4-byte pointers): sizeof == 16. On 64-bit hosts void* is 8
// bytes so the struct is larger — only assert on 32-bit targets.
#ifdef __bada__
static_assert(sizeof(MessageListener) == 16, "MessageListener size mismatch");
#endif

} // namespace Mortar

#endif  // FN_ENTITIES_MESSAGE_H
