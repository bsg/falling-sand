#ifndef WORLD_HPP
#define WORLD_HPP

#include "types.hpp"

template <typename P, template <typename> typename R> struct World;

// clang-format off
template <typename P>
concept IsParticle = requires(P t, bool live, u32 generation) {
    { t.isLive() } -> std::same_as<bool>;
    { t.setLive(live) };
    { t.getGeneration() } -> std::same_as<u8>;
    { t.setGeneration(generation) };
};

template <typename P, template<typename> typename R>
concept IsRule = requires(World<P, R> world, P particle, bool shouldUpdate, Vec2<u32> pos) {
    // TODO constrain this to be static
    { R<P>::step(world, particle, pos, shouldUpdate) };
};
// clang-format on

template <IsParticle P, template <typename> typename R> struct World<P, R> {
    P *mState;
    u8 mGeneration;
    u32 mWidth;
    u32 mHeight;

    World(u32 width, u32 height) {
        mWidth = width;
        mHeight = height;
        mState = (P *)malloc(sizeof(P) * width * height);
        mGeneration = 0;
    }

    void spawn(P p, Vec2<u32> pos) {
        if (pos.x() >= 0 && pos.x() < mWidth && pos.y() >= 0 && pos.y() < mHeight) {
            p.setGeneration(mGeneration);
            *getParticle(pos.x(), pos.y()) = p;
        }
    }

    void swap(P *a, P *b) {
        if (b > a) {
            // b will never be reached in this iteration and if the generation lags behind, it won't
            // be ticked until mGeneration runs a whole cycle. this also means b is not ticked until
            // the next frame but eh...
            b->mGeneration++;
        }

        // TODO this measures as fast as xor swap, look at the asm sometime
        P tmp = *a;
        *a = *b;
        *b = tmp;
    }

    P *getParticle(int x, int y) {
        if (x > mWidth - 1 || x < 0 || y > mHeight - 1 || y < 0) {
            [[unlikely]] return nullptr;
        } else {
            return (mState + y * mWidth + x);
        }
    }

    P *getParticle(Vec2<u32> pos) {
        if (pos.x() > mWidth - 1 || pos.x() < 0 || pos.y() > mHeight - 1 || pos.y() < 0) {
            [[unlikely]] return nullptr;
        } else {
            return getParticle(pos.x(), pos.y());
        }
    }

    void step() {
        for (u32 y = 0; y < mHeight; y++) {
            for (u32 x = 0; x < mWidth; x++) {
                P *p = getParticle(x, y);
                // if (!p->isLive()) {
                //     continue;
                // }

                bool shouldUpdate = false;
                if (p->mGeneration == mGeneration) [[likely]] {
                    shouldUpdate = true;
                    p->mGeneration++;
                }

                R<P>::step(*this, *p, vec2(x, y), shouldUpdate);
            }
        }
        mGeneration++;
    }
};

#endif