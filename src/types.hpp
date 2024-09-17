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

template <const u8 Dim, Scalar T> class Vector {
    T scalars[Dim];

  public:
    Vector<Dim, T>() { bzero(&scalars, sizeof(scalars) * Dim); }

    // TODO std::array has an upper bound but how do I statically enforce a lower bound
    Vector<Dim, T>(std::array<T, Dim> initList) {
        auto it = initList.begin();
        for (auto i = 0; i < Dim; i++) {
            if (it < initList.end()) {
                scalars[i] = *it++;
            } else {
                scalars[i] = 0;
            }
        }
    }

    T &operator[](auto idx) { return scalars[idx]; }
};

template <class T> class Vec2 {
    Vector<2, T> inner;

  public:
    Vec2<T>(const T x, const T y) { inner = Vector<2, T>({x, y}); }

    T &x() { return inner[0]; }
    T &y() { return inner[1]; }
};

#endif