#include "SDL2/SDL_atomic.h"
#include "SDL2/SDL_error.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_mouse.h"
#include "SDL2/SDL_pixels.h"
#include "SDL2/SDL_rect.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_stdinc.h"
#include "SDL2/SDL_timer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <__concepts/same_as.h>
#include <chrono>
#include <iostream>
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

template <typename T> struct vec2 {
    T x;
    T y;

    vec2<T>() {
        x = 0;
        y = 0;
    }

    vec2<T>(T x_, T y_) {
        x = x_;
        y = y_;
    }
};

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 1024

template <typename T> struct World;

// clang-format off
template <typename T>
concept IsParticle = requires(World<T> world, T t, bool shouldUpdate, vec2<u32> pos) {
    { t.isLive() } -> std::same_as<bool>;
    { t.step(world, shouldUpdate, pos) };
};
// clang-format on

template <IsParticle P> struct World<P> {
    P mState[512][512]; // TODO dynamic size
    u8 mGeneration;

    World() {
        bzero(&mState, sizeof(P) * 512 * 512);
        mGeneration = 0;
    }

    void swap(P *a, P *b) {
        if (b > a) {
            // b will never be reached in this iteration and if the generation lags behind, it won't be ticked
            // until mGeneration runs a whole cycle. this also means b is not ticked until the next frame but eh...
            b->mGeneration++;
        }

        // this measures as fast as xor swap
        P tmp = *a;
        *a = *b;
        *b = tmp;
    }

    P *getParticle(int x, int y) {
        if (x > 511 || x < 0 || y > 511 || y < 0) {
            [[unlikely]] return nullptr;
        } else {
            return &mState[y][x];
        }
    }

    void step() {
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 512; x++) {
                P *p = &mState[y][x];
                if (!p->isLive()) {
                    continue;
                }

                bool shouldUpdate = false;
                if (p->mGeneration == mGeneration) [[likely]] {
                    shouldUpdate = true;
                    p->mGeneration++;
                }

                p->step(*this, shouldUpdate, vec2<u32>(x, y));
            }
        }
        mGeneration++;
    }
};

SDL_Window *gWindow = nullptr;
SDL_Surface *gScreenSurface = nullptr;
SDL_Renderer *gRenderer = nullptr;
SDL_Texture *gWorldTexture = nullptr;
u32 *gTexPixels = nullptr;

enum Material : u8 { Air, Sand, Water };

struct ParticleSimple {
    Material mMaterial;
    u8 mGeneration;
    bool mLive;
    u8 mDensity;
    u32 mColor;

    bool isLive() { return mLive; }
    void step(World<ParticleSimple> &world, bool shouldUpdate, vec2<u32> pos);
};

void ParticleSimple::step(World<ParticleSimple> &world, bool shouldUpdate, vec2<u32> pos) {
    if (!shouldUpdate || mMaterial == Air)
        return;

    gTexPixels[pos.y * 512 + pos.x] = mColor;

    auto bottomParticle = world.getParticle(pos.x, pos.y + 1);
    auto bottomLeftParticle = world.getParticle(pos.x - 1, pos.y + 1);
    auto bottomRightParticle = world.getParticle(pos.x + 1, pos.y + 1);

    switch (mMaterial) {
    case Air:
        break;
    case Sand:
        if (bottomParticle && bottomParticle->mDensity < mDensity) {
            world.swap(this, bottomParticle);
        } else if (bottomLeftParticle && bottomLeftParticle->mDensity < mDensity) {
            world.swap(this, bottomLeftParticle);
        } else if (bottomRightParticle && bottomRightParticle->mDensity < mDensity) {
            world.swap(this, bottomRightParticle);
        }
        break;
    case Water:
        auto leftParticle = world.getParticle(pos.x - 1, pos.y);
        auto rightParticle = world.getParticle(pos.x + 1, pos.y);

        if (bottomParticle && bottomParticle->mDensity < mDensity) {
            world.swap(this, bottomParticle);
        } else if (bottomLeftParticle && bottomLeftParticle->mDensity < mDensity) {
            world.swap(this, bottomLeftParticle);
        } else if (bottomRightParticle && bottomRightParticle->mDensity < mDensity) {
            world.swap(this, bottomRightParticle);
        } else if (leftParticle && leftParticle->mDensity < mDensity) {
            world.swap(this, leftParticle);
        } else if (rightParticle && rightParticle->mDensity < mDensity) {
            world.swap(this, rightParticle);
        }
        break;
    }
}

World<ParticleSimple> gWorld;

inline void sdl_bail(std::string s) {
    printf("%s: %s\n", s.c_str(), SDL_GetError());
    exit(1);
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        sdl_bail("SDL_Init");
    }
    gWindow = SDL_CreateWindow("falling sand", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
                               SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (gWindow == nullptr) {
        sdl_bail("SDL_CreateWindow");
    }

    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED);
    if (gRenderer == nullptr) {
        sdl_bail("SDL_CreateRenderer");
    }

    gWorldTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                      SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    if (gWorldTexture == nullptr) {
        sdl_bail("SDL_CreateTexture");
    }

    SDL_SetTextureScaleMode(gWorldTexture, SDL_ScaleModeLinear);

    if (TTF_Init() == -1) {
        sdl_bail("TTF_Init");
    }

    TTF_Font *font = TTF_OpenFont("VCR_OSD_MONO.ttf", 16);
    if (font == nullptr) {
        sdl_bail("TTF_OpenFont");
    }

    SDL_Event e;
    bool quit = false;
    bool spawn = false;

    while (quit == false) [[likely]] {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_KEYDOWN:
                break;
            case SDL_MOUSEBUTTONDOWN:
                spawn = true;
                break;
            case SDL_MOUSEBUTTONUP:
                spawn = false;
                break;
            }
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        if (spawn) {
            int mouseX, mouseY;
            u32 buttonState = SDL_GetMouseState(&mouseX, &mouseY);

            mouseX = mouseX / 2 + rand() % 8 - 4;
            mouseY = mouseY / 2 + rand() % 8 - 4;

            auto p = &gWorld.mState[mouseY][mouseX];
            p->mLive = true;
            if (buttonState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                p->mDensity = 2;
                p->mColor = 0xFFFFCC00 + rand() % 200;
                p->mMaterial = Sand;
            } else if (buttonState & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
                p->mDensity = 1;
                p->mColor = 0xFF0000FF;
                p->mMaterial = Water;
            }
            p->mGeneration = gWorld.mGeneration;
        }

        int pitch;
        void *texBytes = nullptr;
        if (SDL_LockTexture(gWorldTexture, nullptr, &texBytes, &pitch) < 0) [[unlikely]] {
            sdl_bail("SDL_LockTexture");
        }
        gTexPixels = (u32 *)texBytes;

        gWorld.step();

        SDL_UnlockTexture(gWorldTexture);

        SDL_RenderClear(gRenderer);
        SDL_RenderCopy(gRenderer, gWorldTexture, nullptr, nullptr);

        auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
        u128 frameTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

        char buf[1024];
        std::snprintf(buf, 1024, "%.2f FPS (%.3f ms)", 1'000'000.0f / frameTimeUs, frameTimeUs / 1000.0);
        SDL_Surface *surfaceTxt = TTF_RenderText_Blended(font, buf, {255, 255, 255});
        SDL_Texture *texTxt = SDL_CreateTextureFromSurface(gRenderer, surfaceTxt);
        SDL_Rect rectTxt = {0, 0, 0, 0};
        SDL_QueryTexture(texTxt, nullptr, nullptr, &rectTxt.w, &rectTxt.h);
        SDL_RenderCopy(gRenderer, texTxt, nullptr, &rectTxt);

        SDL_RenderPresent(gRenderer);
    }

    return 0;
}