#ifndef FN_ENTITIES_MESSAGE_H
#define FN_ENTITIES_MESSAGE_H

// Mortar::MessageListener and Mortar::Message — 16-byte stub structs.
// Binary layout from ActorManager::SendMessage @ 0x0016ffd8 and
// ActorManager::AddMessageListener @ 0x0017085c.
//
// No active callers in the port yet — declared so ActorManager can compile
// with full SendMessage/AddMessageListener/RemoveMessageListener bodies.
//
// Analysed: 2026-05-04T00:00

namespace Mortar {

struct MessageListener {
    unsigned int msgKindHash;   // +0x00 — filter: 0 = any kind
    unsigned int senderFilter;  // +0x04 — filter: 0 = any sender
    unsigned int typeFilter;    // +0x08 — filter: 0 = any entity type
    void*        callback;      // +0x0C — vtable+0x30 = Notify(Entity*, Message*)
};

struct Message {
    unsigned int msgKindHash;   // +0x04 — what SendMessage dispatch reads
    // remainder opaque; port has no real consumers yet
};

} // namespace Mortar

#endif  // FN_ENTITIES_MESSAGE_H
