#ifndef MORTAR_SINGLETON_H
#define MORTAR_SINGLETON_H

namespace Mortar {

template<typename T>
class Singleton {
public:
    static T& GetInstance() {
        static T instance;
        return instance;
    }

    // Prevent copy/move
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

protected:
    // GCC 4.4 / 4.5 (the cross-build asm-verify toolchain) reject `= default`
    // on protected/private members; written out explicitly for portability.
    Singleton() {}
    ~Singleton() {}
};

} // namespace Mortar
#endif
