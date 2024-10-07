#ifndef TYPES_HPP
#define TYPES_HPP

#include <_types/_uint32_t.h>
#include <_types/_uint64_t.h>
#include <array>
#include <sys/_types/_int32_t.h>
#include <sys/_types/_int64_t.h>
#include <type_traits>

typedef char i8;
typedef unsigned char u8;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
typedef long long i128;
typedef unsigned long long u128;
typedef float f32;

template <class T>
concept Scalar = std::is_arithmetic_v<T>;

// TODO Vector::Vector(T...)
template <const u8 Dim, Scalar T> struct Vector {
    T components[Dim];

    Vector<Dim, T>() { bzero(&components, sizeof(components) * Dim); }

    Vector<Dim, T>(std::array<T, Dim> initList) {
        auto it = initList.begin();
        for (auto i = 0; i < Dim; i++) {
            if (it < initList.end()) {
                components[i] = *it++;
            } else {
                components[i] = 0;
            }
        }
    }

    template <Scalar T2> Vector<Dim, T> operator+(Vector<Dim, T2> rhs) {
        Vector<Dim, T> res;
        for (int i = 0; i < Dim; i++) {
            // TODO SIMD
            res.components[i] = components[i] + rhs.components[i];
        }
        return res;
    }

    T &operator[](auto idx) { return components[idx]; }

    // clang-format off
    T &x() requires(Dim > 0) { return components[0]; }
    T &y() requires(Dim > 1) { return components[1]; }
    T &z() requires(Dim > 2) { return components[2]; }
    // clang-format on
};

template <Scalar T> using Vec2 = Vector<2, T>;
// TODO constrain params to be of the same type
template <Scalar... T> inline Vector<sizeof...(T), std::common_type_t<T...>> vec(T... args) {
    return Vector<sizeof...(T), std::common_type_t<T...>>({args...});
}

#endif