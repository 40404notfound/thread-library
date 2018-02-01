#include <iostream>
#include <cmath>
#include <set>
#include <queue>
#include <type_traits>

template <template <typename ...> class C, typename T, typename = typename std::enable_if<std::is_pointer<T>::value>::type>
class PointerContainer : public C<T> {
public:
    ~PointerContainer() {
        C<T>& ctner = static_cast<C<T>&>(*this);
        for (auto it = ctner.begin(); it != ctner.end(); ++it) {
            delete *it;
        }
    }
};

template <typename T>
class PointerContainer<std::queue, T> : public std::queue<T> {
public:
    ~PointerContainer() {
        std::queue<T>& ctner = static_cast<std::queue<T>&>(*this);
        std::cout << "Should destroy" << std::endl;
        while (!ctner.empty()) {
            T p = ctner.front();
            delete p;
            ctner.pop();
        }
    }
};

PointerContainer<std::queue, int*> pc;

int main() {
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
    pc.push(new int{2});
}