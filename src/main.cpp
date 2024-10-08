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
#include "types.hpp"
#include "world.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <chrono>
#include <iostream>
#include <string>
#include <strings.h>

#define WORLD_WIDTH 500
#define WORLD_HEIGHT 500
#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 1000
#define WORLD_DRAW_SCALE 2

#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif

SDL_Window *gWindow = nullptr;
SDL_Surface *gScreenSurface = nullptr;
SDL_Renderer *gRenderer = nullptr;
SDL_Texture *gWorldTexture = nullptr;
u32 *gTexPixels = nullptr;
TTF_Font *gFont = nullptr;
bool gQuit = false;

inline void sdl_bail(std::string s) {
    printf("%s: %s\n", s.c_str(), SDL_GetError());
    exit(1);
}

struct Particle {
    enum class Material : u8 { Air, Sand, Water, Rock };

    Material mMaterial;
    u8 mGeneration;
    bool mLive;
    u8 mDensity;
    u32 mColor;
    bool mPreferSlideLeft;

    Particle(){};
    Particle(Material material);
    bool isLive() { return mLive; }
    void setLive(bool live) { mLive = live; }
    u8 getGeneration() { return mGeneration; }
    void setGeneration(u8 generation) { mGeneration = generation; }
};

Particle::Particle(Particle::Material material) {
    switch (material) {
    case Particle::Material::Air:
        break;
    case Particle::Material::Sand:
        mDensity = 2;
        mColor = 0xFFFFCC00 + rand() % 200;
        mLive = true;
        break;
    case Particle::Material::Water:
        mDensity = 1;
        mColor = 0xFF0000FF;
        mLive = true;
        break;
    case Particle::Material::Rock:
        mDensity = 255;
        u32 r = rand();
        mColor = 0xFF48443C + ((r % 50) << 16) + ((r % 50) << 8) + (r % 50);
        mLive = false;
        break;
    }

    mMaterial = material;
    mPreferSlideLeft = rand() % 2 == 0;
}

template <IsParticle P> struct Rule {
    static void step(World<P, Rule> &world, P &p, Vec2<u32> pos, bool shouldUpdate);
    static bool trySwapWithAlternate(World<P, Rule> &world, P &particle, Vec2<u32> targetPos,
                                     Vec2<u32> alternatePos);
};

template <IsParticle P>
bool Rule<P>::trySwapWithAlternate(World<P, Rule> &world, P &p, Vec2<u32> targetPos,
                                   Vec2<u32> alternatePos) {
    auto target = world.getParticle(targetPos);
    auto alt = world.getParticle(alternatePos);
    if (target && p.mDensity > target->mDensity) {
        world.swap(&p, target);
        return true;
    } else if (alt && p.mDensity > alt->mDensity) {
        world.swap(&p, alt);
        return true;
    }
    return false;
};

template <IsParticle P>
void Rule<P>::step(World<P, Rule> &world, P &p, Vec2<u32> pos, bool shouldUpdate) {
    gTexPixels[pos.y() * WORLD_WIDTH + pos.x()] = p.mColor;

    if (!shouldUpdate || p.mMaterial == Particle::Material::Air)
        return;

    auto bottomParticle = world.getParticle(pos + vec(0, 1));
    auto bottomLeftParticle = world.getParticle(pos + vec(-1, 1));
    auto bottomRightParticle = world.getParticle(pos + vec(1, 1));

    switch (p.mMaterial) {
    case Particle::Material::Sand:

        if (bottomParticle && bottomParticle->mDensity < p.mDensity) {
            world.swap(&p, bottomParticle);
        } else {
            if (p.mPreferSlideLeft) {
                trySwapWithAlternate(world, p, pos + vec(-1, 1), pos + vec(1, 1));
            } else {
                trySwapWithAlternate(world, p, pos + vec(1, 1), pos + vec(-1, 1));
            }
        }
        break;
    case Particle::Material::Water:
        if (bottomParticle && bottomParticle->mDensity < p.mDensity) {
            world.swap(&p, bottomParticle);
        } else {
            bool swapped = false;
            if (p.mPreferSlideLeft) {
                swapped = trySwapWithAlternate(world, p, pos + vec(-1, 1), pos + vec(1, 1));
            } else {
                swapped = trySwapWithAlternate(world, p, pos + vec(1, 1), pos + vec(-1, 1));
            }

            if (!swapped) {
                if (p.mPreferSlideLeft) {
                    trySwapWithAlternate(world, p, pos + vec(-1, 0), pos + vec(1, 0));
                } else {
                    trySwapWithAlternate(world, p, pos + vec(1, 0), pos + vec(-1, 0));
                }
            }
        }
        break;
    }
}

World<Particle, Rule> gWorld(WORLD_WIDTH, WORLD_HEIGHT);

Particle::Material gSelectedMaterial = Particle::Material::Sand;

enum class BrushMode : u8 { Spray, Paint };
BrushMode gSelectedBrushMode = BrushMode::Spray;

bool gSpawn = false; // TODO put this in a ctx that will be passed to mainLoop
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
            case SDLK_ESCAPE:
                gQuit = true;
                break;
            case SDLK_1:
                gSelectedMaterial = Particle::Material::Sand;
                break;
            case SDLK_2:
                gSelectedMaterial = Particle::Material::Water;
                break;
            case SDLK_3:
                gSelectedMaterial = Particle::Material::Rock;
                break;
            case SDLK_0:
                gSelectedMaterial = Particle::Material::Air;
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
            Particle p(gSelectedMaterial);

            u32 spawnX, spawnY;
            const int SPAWN_DISPERSION = 4;

            switch (gSelectedBrushMode) {
            case BrushMode::Spray:
                spawnX =
                    mouseX / WORLD_DRAW_SCALE + rand() % (SPAWN_DISPERSION * 2) - SPAWN_DISPERSION;
                spawnY =
                    mouseY / WORLD_DRAW_SCALE + rand() % (SPAWN_DISPERSION * 2) - SPAWN_DISPERSION;

                gWorld.spawn(p, vec(spawnX, spawnY));
                break;
            case BrushMode::Paint:
                for (int offsetX = -2; offsetX <= 2; offsetX++) {
                    for (int offsetY = -2; offsetY <= 2; offsetY++) {
                        p = Particle(gSelectedMaterial);
                        gWorld.spawn(
                            p, vec(static_cast<unsigned int>(mouseX / WORLD_DRAW_SCALE + offsetX),
                                   static_cast<unsigned int>(mouseY / WORLD_DRAW_SCALE + offsetY)));
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