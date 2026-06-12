#ifndef FN_MORTAR_EVENT_H
#define FN_MORTAR_EVENT_H

//
// Mortar::EventN<Args...> — fixed-arity multicast signal, matching the
// binary's EventN layout.
//
// Binary spec (v1.6.1):
//   Event1<Fruit*>::Event1() @ real 0x001e359c  -> std::list<Delegate1>::list(this)
//   Event3<Fruit*,int,Mortar::Entity*>::Event3() @ real 0x001e3548 -> same pattern
//
// Layout: EventN is literally a std::list<DelegateN<void, ...>>.
//   8 bytes on ARM32 (pre-C++11 Sourcery sentinel-only list: {prev,next}).
//
// operator+= (Subscribe): push_back a copy of the delegate.
// operator-= (Unsubscribe): find matching delegate (by obj+pmf/fnptr equality
//   via raw memory compare of the inline buffer) and erase. The binary's
//   equality check compares the stored subobject bytes from the inline buffer.
// operator() (Fire): iterate front-to-back, call each delegate.
//
// Fixed-arity (Event0/1/2/3) — NOT variadic — to match the pre-C++11 binary
// and stay GCC-4.4 cross-build safe (no variadic templates).
//

#include "Delegate.h"
#include <list>
#include <cstring>

namespace Mortar {

// ---------------------------------------------------------------------------
// Helper: delegate equality for operator-=.
// The binary compares the inline storage to find a matching subscriber.
// We compare the canonical 36 bytes (32B storage + 1B flag + 3B pad) rather
// than sizeof(TDelegate) to avoid comparing compiler-inserted tail padding
// on 64-bit hosts where sizeof may be 40.
// ---------------------------------------------------------------------------

template<typename TDelegate>
static inline bool DelegateEqual(const TDelegate& a, const TDelegate& b) {
    // 36 = kInlineSize(32) + m_bEmpty(1) + m_pad(3).
    // Both delegates have zeroed storage + pad in their constructors,
    // so the 36-byte comparison is deterministic.
    return ::memcmp(&a, &b, 36) == 0;
}

// ---------------------------------------------------------------------------
// Event0<void> — no-arg multicast signal.
// Binary RTTI: Mortar/Event0<void> (not confirmed in spec but kept for
// completeness and API symmetry).
// ---------------------------------------------------------------------------
class Event0 {
public:
    typedef Delegate0<void> DelegateT;
    typedef std::list<DelegateT> ListT;

    Event0() {}
    ~Event0() {}

    Event0& operator+=(const DelegateT& d) {
        m_List.push_back(d);
        return *this;
    }

    Event0& operator-=(const DelegateT& d) {
        for (ListT::iterator it = m_List.begin(); it != m_List.end(); ++it) {
            if (DelegateEqual(*it, d)) {
                m_List.erase(it);
                return *this;
            }
        }
        return *this;
    }

    void operator()() const {
        for (ListT::const_iterator it = m_List.begin(); it != m_List.end(); ++it) {
            (*it)();
        }
    }

private:
    ListT m_List;
};

// ---------------------------------------------------------------------------
// Event1<A1> — 1-arg multicast signal.
// Binary RTTI: Mortar/Event1<Fruit*>, Mortar/Event1<SlashEntity*>.
// ---------------------------------------------------------------------------
template<typename A1>
class Event1 {
public:
    typedef Delegate1<void, A1> DelegateT;
    typedef std::list<DelegateT> ListT;

    Event1() {}
    ~Event1() {}

    Event1& operator+=(const DelegateT& d) {
        m_List.push_back(d);
        return *this;
    }

    Event1& operator-=(const DelegateT& d) {
        for (typename ListT::iterator it = m_List.begin(); it != m_List.end(); ++it) {
            if (DelegateEqual(*it, d)) {
                m_List.erase(it);
                return *this;
            }
        }
        return *this;
    }

    void operator()(A1 a1) const {
        for (typename ListT::const_iterator it = m_List.begin(); it != m_List.end(); ++it) {
            (*it)(a1);
        }
    }

private:
    ListT m_List;
};

// ---------------------------------------------------------------------------
// Event2<A1,A2> — 2-arg multicast signal.
// Binary RTTI: Mortar/Event2<int,int>, Mortar/Event2<Mortar::Orientation,bool&>.
// ---------------------------------------------------------------------------
template<typename A1, typename A2>
class Event2 {
public:
    typedef Delegate2<void, A1, A2> DelegateT;
    typedef std::list<DelegateT> ListT;

    Event2() {}
    ~Event2() {}

    Event2& operator+=(const DelegateT& d) {
        m_List.push_back(d);
        return *this;
    }

    Event2& operator-=(const DelegateT& d) {
        for (typename ListT::iterator it = m_List.begin(); it != m_List.end(); ++it) {
            if (DelegateEqual(*it, d)) {
                m_List.erase(it);
                return *this;
            }
        }
        return *this;
    }

    void operator()(A1 a1, A2 a2) const {
        for (typename ListT::const_iterator it = m_List.begin(); it != m_List.end(); ++it) {
            (*it)(a1, a2);
        }
    }

private:
    ListT m_List;
};

// ---------------------------------------------------------------------------
// Event3<A1,A2,A3> — 3-arg multicast signal.
// Binary RTTI: Mortar/Event3<Fruit*,int,Mortar::Entity*>.
// ---------------------------------------------------------------------------
template<typename A1, typename A2, typename A3>
class Event3 {
public:
    typedef Delegate3<void, A1, A2, A3> DelegateT;
    typedef std::list<DelegateT> ListT;

    Event3() {}
    ~Event3() {}

    Event3& operator+=(const DelegateT& d) {
        m_List.push_back(d);
        return *this;
    }

    Event3& operator-=(const DelegateT& d) {
        for (typename ListT::iterator it = m_List.begin(); it != m_List.end(); ++it) {
            if (DelegateEqual(*it, d)) {
                m_List.erase(it);
                return *this;
            }
        }
        return *this;
    }

    void operator()(A1 a1, A2 a2, A3 a3) const {
        for (typename ListT::const_iterator it = m_List.begin(); it != m_List.end(); ++it) {
            (*it)(a1, a2, a3);
        }
    }

private:
    ListT m_List;
};

} // namespace Mortar

#endif // FN_MORTAR_EVENT_H
