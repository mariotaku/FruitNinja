// SmartPtr<T> / ReferenceCounter unit test.
//
// Covers: default null, raw-ptr ctor (AddRef), copy ctor, scope-exit Release,
// copy-assign AddRef-before-Release ordering, aliased-assign safety, self-assign,
// raw-ptr assign, SetPtr/SetNull, operator->/*, IsValid(), operator==, operator!=,
// WrapPtr ownership transfer.
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Minimal ref-counted type with a static live-instance counter.
// Derives from ReferenceCounter; counter increments in ctor, decrements in dtor.
// Lets us verify the object is actually destroyed when refcount -> 0.
// ---------------------------------------------------------------------------
static int g_liveCount = 0;

class TestObj : public Mortar::ReferenceCounter {
public:
    int value;
    explicit TestObj(int v) : value(v) { g_liveCount++; }
    virtual ~TestObj() { g_liveCount--; }
};

// ---------------------------------------------------------------------------
// test_default_null
// ---------------------------------------------------------------------------
static void test_default_null()
{
    Mortar::SmartPtr<TestObj> p;
    CHECK(!p.IsValid());
    CHECK(!p);
    CHECK(p.Get() == 0);
    CHECK(p == Mortar::SmartPtr<TestObj>());
}

// ---------------------------------------------------------------------------
// test_raw_ptr_ctor
// ---------------------------------------------------------------------------
static void test_raw_ptr_ctor()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(42);
    CHECK(g_liveCount == 1);
    CHECK(raw->GetRefCount() == 0);

    {
        Mortar::SmartPtr<TestObj> p(raw);
        CHECK(p.IsValid());
        CHECK(p.Get() == raw);
        CHECK(raw->GetRefCount() == 1);
        CHECK(g_liveCount == 1);
    }
    // SmartPtr dtor fired Release() -> refcount 0 -> delete -> dtor.
    CHECK(g_liveCount == 0);
}

// ---------------------------------------------------------------------------
// test_copy_ctor
// ---------------------------------------------------------------------------
static void test_copy_ctor()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(7);
    {
        Mortar::SmartPtr<TestObj> a(raw);
        CHECK(raw->GetRefCount() == 1);
        {
            Mortar::SmartPtr<TestObj> b(a);
            CHECK(b.IsValid());
            CHECK(b.Get() == raw);
            CHECK(raw->GetRefCount() == 2);
            CHECK(a == b);
            CHECK(g_liveCount == 1);
        }
        // b destroyed -> refcount 1, object still alive.
        CHECK(raw->GetRefCount() == 1);
        CHECK(g_liveCount == 1);
    }
    // a destroyed -> refcount 0 -> object deleted.
    CHECK(g_liveCount == 0);
}

// ---------------------------------------------------------------------------
// test_copy_assign_different_objects
// Assigning a=b where a and b point to different objects.
// The implementation does: AddRef(b.obj) THEN Release(a.obj) -- safe ordering.
// After assign: a.obj (obj1) refcount should have hit 0 -> deleted; obj2 refcount==2.
// ---------------------------------------------------------------------------
static void test_copy_assign_different_objects()
{
    g_liveCount = 0;
    TestObj* raw1 = new TestObj(1);
    TestObj* raw2 = new TestObj(2);
    CHECK(g_liveCount == 2);

    Mortar::SmartPtr<TestObj> a(raw1);
    Mortar::SmartPtr<TestObj> b(raw2);
    CHECK(raw1->GetRefCount() == 1);
    CHECK(raw2->GetRefCount() == 1);

    // a = b: AddRef(raw2) -> refcount 2; Release(raw1) -> refcount 0 -> raw1 deleted.
    a = b;

    CHECK(g_liveCount == 1);               // raw1 deleted
    CHECK(a.Get() == raw2);
    CHECK(raw2->GetRefCount() == 2);       // a and b both hold raw2
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
// test_copy_assign_same_object_aliasing
// Assigning a=b where both already point to the same object.
// operator= is guarded by (this != &o); a=b with different SmartPtr objects
// pointing to the same raw pointer must NOT trigger a double-free.
// Sequence: AddRef(raw) -> refcount 2; Release(raw) -> refcount 1; m_ptr unchanged.
// ---------------------------------------------------------------------------
static void test_copy_assign_same_object_aliasing()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(99);

    Mortar::SmartPtr<TestObj> a(raw);  // refcount 1
    Mortar::SmartPtr<TestObj> b(a);    // refcount 2

    // Now assign a = b when both point to the same object.
    // The implementation is NOT guarded by pointer equality in the SmartPtr= overload,
    // only by (this != &o). So AddRef fires (->3) then Release fires (->2). Object survives.
    a = b;

    CHECK(g_liveCount == 1);
    CHECK(a.Get() == raw);
    CHECK(raw->GetRefCount() == 2);
    // a == b still
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
// test_self_assign
// a = a: operator= guard (this != &o) makes this a no-op.
// Refcount and liveness must be unchanged.
// ---------------------------------------------------------------------------
static void test_self_assign()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(5);

    Mortar::SmartPtr<TestObj> a(raw);
    CHECK(raw->GetRefCount() == 1);

    a = a;  // self-assign -- must not double-free or change refcount

    CHECK(a.IsValid());
    CHECK(a.Get() == raw);
    CHECK(raw->GetRefCount() == 1);
    CHECK(g_liveCount == 1);
}

// ---------------------------------------------------------------------------
// test_raw_ptr_assign
// operator=(T*) raw pointer variant.
// ---------------------------------------------------------------------------
static void test_raw_ptr_assign()
{
    g_liveCount = 0;
    TestObj* raw1 = new TestObj(10);
    TestObj* raw2 = new TestObj(20);
    CHECK(g_liveCount == 2);

    Mortar::SmartPtr<TestObj> p(raw1);
    CHECK(raw1->GetRefCount() == 1);

    p = raw2;  // AddRef(raw2), Release(raw1) -> raw1 deleted.
    CHECK(g_liveCount == 1);
    CHECK(p.Get() == raw2);
    CHECK(raw2->GetRefCount() == 1);

    // Self-assign by raw pointer: p = p.Get() -> guarded by (p != m_ptr) -> no-op.
    TestObj* same = p.Get();
    p = same;
    CHECK(raw2->GetRefCount() == 1);
    CHECK(g_liveCount == 1);
}

// ---------------------------------------------------------------------------
// test_set_ptr_and_set_null
// ---------------------------------------------------------------------------
static void test_set_ptr_and_set_null()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(3);

    Mortar::SmartPtr<TestObj> p;
    CHECK(!p.IsValid());

    p.SetPtr(raw);
    CHECK(p.IsValid());
    CHECK(raw->GetRefCount() == 1);

    p.SetNull();
    CHECK(!p.IsValid());
    CHECK(g_liveCount == 0);  // raw deleted
}

// ---------------------------------------------------------------------------
// test_deref_operators
// operator-> and operator* must return the managed object.
// ---------------------------------------------------------------------------
static void test_deref_operators()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(77);

    Mortar::SmartPtr<TestObj> p(raw);
    CHECK(p->value == 77);
    CHECK((*p).value == 77);
}

// ---------------------------------------------------------------------------
// test_equality_operators
// ---------------------------------------------------------------------------
static void test_equality_operators()
{
    g_liveCount = 0;
    TestObj* raw1 = new TestObj(1);
    TestObj* raw2 = new TestObj(2);

    Mortar::SmartPtr<TestObj> a(raw1);
    Mortar::SmartPtr<TestObj> b(raw2);
    Mortar::SmartPtr<TestObj> c(a);  // same as a

    CHECK(a != b);
    CHECK(!(a == b));
    CHECK(a == c);
    CHECK(!(a != c));
}

// ---------------------------------------------------------------------------
// test_wrap_ptr
// WrapPtr(new T) transfers ownership: new'd object starts at refcount 0,
// SmartPtr ctor AddRefs it to 1.
// ---------------------------------------------------------------------------
static void test_wrap_ptr()
{
    g_liveCount = 0;
    {
        Mortar::SmartPtr<TestObj> p = Mortar::WrapPtr(new TestObj(55));
        CHECK(p.IsValid());
        CHECK(p->value == 55);
        CHECK(p.Get()->GetRefCount() == 1);
        CHECK(g_liveCount == 1);
    }
    CHECK(g_liveCount == 0);  // SmartPtr dtor deleted the object
}

// ---------------------------------------------------------------------------
// test_scope_chain
// Chain of copies: object only deleted after the last SmartPtr goes away.
// ---------------------------------------------------------------------------
static void test_scope_chain()
{
    g_liveCount = 0;
    TestObj* raw = new TestObj(0);

    Mortar::SmartPtr<TestObj> a(raw);
    CHECK(raw->GetRefCount() == 1);

    {
        Mortar::SmartPtr<TestObj> b(a);
        CHECK(raw->GetRefCount() == 2);
        {
            Mortar::SmartPtr<TestObj> cc(b);
            CHECK(raw->GetRefCount() == 3);
        }
        // cc gone -> refcount 2
        CHECK(raw->GetRefCount() == 2);
        CHECK(g_liveCount == 1);
    }
    // b gone -> refcount 1
    CHECK(raw->GetRefCount() == 1);
    CHECK(g_liveCount == 1);
    // a goes out at end of function -> dtor -> refcount 0 -> deleted
    // (verified after the call returns in main)
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::printf("test_smartptr: start\n");

    test_default_null();
    std::printf("  default null: OK\n");

    test_raw_ptr_ctor();
    std::printf("  raw-ptr ctor AddRef + scope-exit Release: OK\n");

    test_copy_ctor();
    std::printf("  copy ctor refcount==2, last-out deletes: OK\n");

    test_copy_assign_different_objects();
    std::printf("  copy assign different objects (AddRef-before-Release): OK\n");

    test_copy_assign_same_object_aliasing();
    std::printf("  copy assign aliased ptrs (same obj, different SmartPtr): OK\n");

    test_self_assign();
    std::printf("  self-assign (this==&o guard, no double-free): OK\n");

    test_raw_ptr_assign();
    std::printf("  raw-ptr assign + aliased raw self-assign: OK\n");

    test_set_ptr_and_set_null();
    std::printf("  SetPtr / SetNull lifecycle: OK\n");

    test_deref_operators();
    std::printf("  operator-> / operator*: OK\n");

    test_equality_operators();
    std::printf("  operator== / operator!=: OK\n");

    test_wrap_ptr();
    std::printf("  WrapPtr ownership transfer: OK\n");

    {
        // test_scope_chain uses a local raw pointer; after the call returns the
        // local SmartPtr 'a' is destroyed and g_liveCount should drop to 0.
        int before = g_liveCount;
        (void)before;
        test_scope_chain();
    }
    CHECK(g_liveCount == 0);
    std::printf("  scope chain (nested copy, last-out deletes): OK\n");

    std::printf("test_smartptr: PASS\n");
    return 0;
}
