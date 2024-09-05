#include "SDL2/SDL_error.h"
#include "SDL2/SDL_events.h"
#include "SDL2/SDL_keycode.h"
#include "SDL2/SDL_mouse.h"
#include "SDL2/SDL_pixels.h"
#include "SDL2/SDL_rect.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_stdinc.h"
#include "SDL2/SDL_surface.h"
#include "SDL2/SDL_timer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <__concepts/same_as.h>
#include <chrono>
#include <iostream>
#include <string>

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

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

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 1000
#define WORLD_WIDTH 500
#define WORLD_HEIGHT 500
#define WORLD_DRAW_SCALE 2

template <typename T> struct World;

// clang-format off
template <typename T>
concept IsParticle = requires(World<T> world, T t, bool shouldUpdate, vec2<u32> pos, bool live, u32 generation) {
    { t.isLive() } -> std::same_as<bool>;
    { t.setLive(live) };
    { t.getGeneration() } -> std::same_as<u8>;
    { t.setGeneration(generation) };
    { t.step(world, shouldUpdate, pos) };
};
// clang-format on

template <IsParticle P> struct World<P> {
    P mState[WORLD_HEIGHT][WORLD_WIDTH];
    u8 mGeneration;

    World() {
        bzero(&mState, sizeof(P) * WORLD_HEIGHT * WORLD_WIDTH);
        mGeneration = 0;
    }

    void spawn(P p, vec2<u32> pos) {
        if (pos.x >= 0 && pos.x < WORLD_WIDTH && pos.y >= 0 && pos.y < WORLD_HEIGHT) {
            p.setGeneration(mGeneration);
            mState[pos.y][pos.x] = p;
        }
    }

    void swap(P *a, P *b) {
        if (b > a) {
            // b will never be reached in this iteration and if the generation lags behind, it won't
            // be ticked until mGeneration runs a whole cycle. this also means b is not ticked until
            // the next frame but eh...
            b->mGeneration++;
        }

        // this measures as fast as xor swap
        P tmp = *a;
        *a = *b;
        *b = tmp;
    }

    P *getParticle(int x, int y) {
        if (x > WORLD_WIDTH - 1 || x < 0 || y > WORLD_HEIGHT - 1 || y < 0) {
            [[unlikely]] return nullptr;
        } else {
            return &mState[y][x];
        }
    }

    void step() {
        for (int y = 0; y < WORLD_HEIGHT; y++) {
            for (int x = 0; x < WORLD_WIDTH; x++) {
                P *p = &mState[y][x];
                // if (!p->isLive()) {
                //     continue;
                // }

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
TTF_Font *gFont = nullptr;
bool gQuit = false;

struct ParticleSimple {
    enum class Material : u8 { Air, Sand, Water, Rock };

    Material mMaterial;
    u8 mGeneration;
    bool mLive;
    u8 mDensity;
    u32 mColor;
    bool mPreferSlideLeft;

    ParticleSimple(){};
    ParticleSimple(Material material);
    bool isLive() { return mLive; }
    void setLive(bool live) { mLive = live; }
    u8 getGeneration() { return mGeneration; }
    void setGeneration(u8 generation) { mGeneration = generation; }
    bool trySwapWithAlternate(World<ParticleSimple> &world, vec2<u32> targetPos,
                              vec2<u32> alternatePos);
    void step(World<ParticleSimple> &world, bool shouldUpdate, vec2<u32> pos);
};

ParticleSimple::ParticleSimple(ParticleSimple::Material material) {
    switch (material) {
    case ParticleSimple::Material::Air:
        break;
    case ParticleSimple::Material::Sand:
        mDensity = 2;
        mColor = 0xFFFFCC00 + rand() % 200;
        mLive = true;
        break;
    case ParticleSimple::Material::Water:
        mDensity = 1;
        mColor = 0xFF0000FF;
        mLive = true;
        break;
    case ParticleSimple::Material::Rock:
        mDensity = 255;
        u32 r = rand();
        mColor = 0xFF48443C + ((r % 50) << 16) + ((r % 50) << 8) + (r % 50);
        mLive = false;
        break;
    }

    mMaterial = material;
    mPreferSlideLeft = rand() % 2 == 0;
}

bool ParticleSimple::trySwapWithAlternate(World<ParticleSimple> &world, vec2<u32> targetPos,
                                          vec2<u32> alternatePos) {
    auto p = world.getParticle(targetPos.x, targetPos.y);
    auto altP = world.getParticle(alternatePos.x, alternatePos.y);
    if (p && p->mDensity < mDensity) {
        world.swap(this, p);
        return true;
    } else if (altP && altP->mDensity < mDensity) {
        world.swap(this, altP);
        return true;
    }

    return false;
}

void ParticleSimple::step(World<ParticleSimple> &world, bool shouldUpdate, vec2<u32> pos) {
    gTexPixels[pos.y * WORLD_WIDTH + pos.x] = mColor;

    if (!shouldUpdate || mMaterial == ParticleSimple::Material::Air)
        return;

    auto bottomParticle = world.getParticle(pos.x, pos.y + 1);
    auto bottomLeftParticle = world.getParticle(pos.x - 1, pos.y + 1);
    auto bottomRightParticle = world.getParticle(pos.x + 1, pos.y + 1);

    switch (mMaterial) {
    case ParticleSimple::Material::Air:
        break;
    case ParticleSimple::Material::Sand:
        if (bottomParticle && bottomParticle->mDensity < mDensity) {
            world.swap(this, bottomParticle);
        } else {
            if (mPreferSlideLeft) {
                trySwapWithAlternate(world, vec2<u32>(pos.x - 1, pos.y + 1),
                                     vec2<u32>(pos.x + 1, pos.y + 1));
            } else {
                trySwapWithAlternate(world, vec2<u32>(pos.x + 1, pos.y + 1),
                                     vec2<u32>(pos.x - 1, pos.y + 1));
            }
        }
        break;
    case ParticleSimple::Material::Water:
        if (bottomParticle && bottomParticle->mDensity < mDensity) {
            world.swap(this, bottomParticle);
        } else {
            bool swapped = false;
            if (mPreferSlideLeft) {
                swapped = trySwapWithAlternate(world, vec2<u32>(pos.x - 1, pos.y + 1),
                                               vec2<u32>(pos.x + 1, pos.y + 1));
            } else {
                swapped = trySwapWithAlternate(world, vec2<u32>(pos.x + 1, pos.y + 1),
                                               vec2<u32>(pos.x - 1, pos.y + 1));
            }

            if (!swapped) {
                if (mPreferSlideLeft) {
                    trySwapWithAlternate(world, vec2<u32>(pos.x - 1, pos.y),
                                         vec2<u32>(pos.x + 1, pos.y));
                } else {
                    trySwapWithAlternate(world, vec2<u32>(pos.x + 1, pos.y),
                                         vec2<u32>(pos.x - 1, pos.y));
                }
            }
        }
        break;
    case ParticleSimple::Material::Rock:
        break;
    }
}

World<ParticleSimple> gWorld;

inline void sdl_bail(std::string s) {
    printf("%s: %s\n", s.c_str(), SDL_GetError());
    exit(1);
}

ParticleSimple::Material gSelectedMaterial = ParticleSimple::Material::Air;

enum class BrushMode : u8 { Spray, Paint };
BrushMode gSelectedBrushMode = BrushMode::Spray;

bool gSpawn = false;
inline void mainLoop() {
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            gQuit = true;

#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#endif
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_1:
                gSelectedMaterial = ParticleSimple::Material::Sand;
                break;
            case SDLK_2:
                gSelectedMaterial = ParticleSimple::Material::Water;
                break;
            case SDLK_3:
                gSelectedMaterial = ParticleSimple::Material::Rock;
                break;
            case SDLK_0:
                gSelectedMaterial = ParticleSimple::Material::Air;
                break;
            case SDLK_q:
                gSelectedBrushMode = BrushMode::Spray;
                break;
            case SDLK_w:
                gSelectedBrushMode = BrushMode::Paint;
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            gSpawn = true;
            break;
        case SDL_MOUSEBUTTONUP:
            gSpawn = false;
            break;
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    if (gSpawn) {
        int mouseX, mouseY;
        u32 buttonState = SDL_GetMouseState(&mouseX, &mouseY);

        if (buttonState & SDL_BUTTON(SDL_BUTTON_LEFT)) {
            ParticleSimple p(gSelectedMaterial);

            u32 spawnX, spawnY;
            const int SPAWN_DISPERSION = 4;

            switch (gSelectedBrushMode) {
            case BrushMode::Spray:
                spawnX =
                    mouseX / WORLD_DRAW_SCALE + rand() % (SPAWN_DISPERSION * 2) - SPAWN_DISPERSION;
                spawnY =
                    mouseY / WORLD_DRAW_SCALE + rand() % (SPAWN_DISPERSION * 2) - SPAWN_DISPERSION;

                gWorld.spawn(p, vec2<u32>(spawnX, spawnY));
                break;
            case BrushMode::Paint:
                for (int offsetX = -2; offsetX <= 2; offsetX++) {
                    for (int offsetY = -2; offsetY <= 2; offsetY++) {
                        p = ParticleSimple(gSelectedMaterial);
                        gWorld.spawn(p, vec2<u32>(mouseX / WORLD_DRAW_SCALE + offsetX,
                                                  mouseY / WORLD_DRAW_SCALE + offsetY));
                    }
                }
                break;
            }
        }
    }

#ifdef __EMSCRIPTEN__
    SDL_DestroyTexture(gWorldTexture);
    gWorldTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_HEIGHT);
    if (gWorldTexture == nullptr) [[unlikely]] {
        sdl_bail("SDL_CreateTexture");
    }
#endif

    int pitch;
    void *texBytes = nullptr;
    if (SDL_LockTexture(gWorldTexture, nullptr, &texBytes, &pitch) < 0) [[unlikely]] {
        sdl_bail("SDL_LockTexture");
    }
    gTexPixels = (u32 *)texBytes;

    gWorld.step();

    SDL_UnlockTexture(gWorldTexture);

    SDL_SetRenderDrawColor(gRenderer, 33, 33, 33, 255);
    SDL_RenderClear(gRenderer);

    SDL_Rect rect = {
        .x = 0, .y = 0, .w = WORLD_WIDTH * WORLD_DRAW_SCALE, .h = WORLD_HEIGHT * WORLD_DRAW_SCALE};
    SDL_RenderCopy(gRenderer, gWorldTexture, nullptr, &rect);

    auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
    u128 frameTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

    static std::string txtBrushMaterials[] = {"AIR", "SAND", "WATER", "ROCK"};
    static std::string txtBrushModes[] = {"SPRAY", "PAINT"};

    char buf[1024];
    std::snprintf(buf, 1024, "%07.2f TPS (%06.3f ms)\nBRUSH: %s %s", 1'000'000.0f / frameTimeUs,
                  frameTimeUs / 1000.0, txtBrushModes[(u32)gSelectedBrushMode].c_str(),
                  txtBrushMaterials[(u32)gSelectedMaterial].c_str());
    SDL_Surface *surfaceTxt = TTF_RenderText_Blended_Wrapped(gFont, buf, {255, 255, 255}, 0);
    SDL_Texture *texTxt = SDL_CreateTextureFromSurface(gRenderer, surfaceTxt);
    SDL_Rect rectTxt = {0, 0, 0, 0};
    SDL_QueryTexture(texTxt, nullptr, nullptr, &rectTxt.w, &rectTxt.h);
    SDL_RenderCopy(gRenderer, texTxt, nullptr, &rectTxt);
    SDL_DestroyTexture(texTxt);
    SDL_FreeSurface(surfaceTxt);

    SDL_RenderPresent(gRenderer);
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        sdl_bail("SDL_Init");
    }
    gWindow = SDL_CreateWindow("falling sand", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (gWindow == nullptr) {
        sdl_bail("SDL_CreateWindow");
    }

    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED);
    if (gRenderer == nullptr) {
        sdl_bail("SDL_CreateRenderer");
    }

    gWorldTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_HEIGHT);
    if (gWorldTexture == nullptr) {
        sdl_bail("SDL_CreateTexture");
    }

    SDL_SetTextureScaleMode(gWorldTexture, SDL_ScaleModeLinear);

    if (TTF_Init() == -1) {
        sdl_bail("TTF_Init");
    }

    gFont = TTF_OpenFont("./assets/VCR_OSD_MONO.ttf", 16);
    if (gFont == nullptr) {
        sdl_bail("TTF_OpenFont");
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoop, 0, 1);
#else
    while (gQuit == false) [[likely]] {
        mainLoop();
    }
#endif

    return 0;
}