#ifndef MORTAR_DELEGATE_H
#define MORTAR_DELEGATE_H

#include <functional>
#include <cstddef>

// Delegate0 through Delegate4 -- type-safe callback system
// Original: 36-byte StackAllocatedPointer<BaseDelegate, 32> with inline storage.
// Port: uses std::function for simplicity while matching the original API.

// QCallee factory: creates a delegate from a member function pointer + object pointer
// Usage: Delegate0<void>::QCallee<MyClass>(obj, &MyClass::Method)

template<typename Ret>
class Delegate0 {
    std::function<Ret()> m_func;
public:
    Delegate0() {}

    Delegate0(Ret (*func)()) : m_func(func) {}

    template<typename T>
    static Delegate0 QCallee(T* obj, Ret (T::*method)()) {
        Delegate0 d;
        d.m_func = [obj, method]() -> Ret { return (obj->*method)(); };
        return d;
    }

    Ret operator()() const { return m_func(); }
    operator bool() const { return static_cast<bool>(m_func); }
};

template<typename Ret, typename A1>
class Delegate1 {
    std::function<Ret(A1)> m_func;
public:
    Delegate1() {}

    Delegate1(Ret (*func)(A1)) : m_func(func) {}

    template<typename T>
    static Delegate1 QCallee(T* obj, Ret (T::*method)(A1)) {
        Delegate1 d;
        d.m_func = [obj, method](A1 a1) -> Ret { return (obj->*method)(a1); };
        return d;
    }

    Ret operator()(A1 a1) const { return m_func(a1); }
    operator bool() const { return static_cast<bool>(m_func); }
};

template<typename Ret, typename A1, typename A2>
class Delegate2 {
    std::function<Ret(A1, A2)> m_func;
public:
    Delegate2() {}

    Delegate2(Ret (*func)(A1, A2)) : m_func(func) {}

    template<typename T>
    static Delegate2 QCallee(T* obj, Ret (T::*method)(A1, A2)) {
        Delegate2 d;
        d.m_func = [obj, method](A1 a1, A2 a2) -> Ret { return (obj->*method)(a1, a2); };
        return d;
    }

    Ret operator()(A1 a1, A2 a2) const { return m_func(a1, a2); }
    operator bool() const { return static_cast<bool>(m_func); }
};

template<typename Ret, typename A1, typename A2, typename A3>
class Delegate3 {
    std::function<Ret(A1, A2, A3)> m_func;
public:
    Delegate3() {}

    Delegate3(Ret (*func)(A1, A2, A3)) : m_func(func) {}

    template<typename T>
    static Delegate3 QCallee(T* obj, Ret (T::*method)(A1, A2, A3)) {
        Delegate3 d;
        d.m_func = [obj, method](A1 a1, A2 a2, A3 a3) -> Ret {
            return (obj->*method)(a1, a2, a3);
        };
        return d;
    }

    Ret operator()(A1 a1, A2 a2, A3 a3) const { return m_func(a1, a2, a3); }
    operator bool() const { return static_cast<bool>(m_func); }
};

template<typename Ret, typename A1, typename A2, typename A3, typename A4>
class Delegate4 {
    std::function<Ret(A1, A2, A3, A4)> m_func;
public:
    Delegate4() {}

    Delegate4(Ret (*func)(A1, A2, A3, A4)) : m_func(func) {}

    template<typename T>
    static Delegate4 QCallee(T* obj, Ret (T::*method)(A1, A2, A3, A4)) {
        Delegate4 d;
        d.m_func = [obj, method](A1 a1, A2 a2, A3 a3, A4 a4) -> Ret {
            return (obj->*method)(a1, a2, a3, a4);
        };
        return d;
    }

    Ret operator()(A1 a1, A2 a2, A3 a3, A4 a4) const { return m_func(a1, a2, a3, a4); }
    operator bool() const { return static_cast<bool>(m_func); }
};

#endif // MORTAR_DELEGATE_H
