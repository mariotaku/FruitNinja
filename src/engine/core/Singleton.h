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
    Singleton() = default;
    ~Singleton() = default;
};

} // namespace Mortar
#endif
