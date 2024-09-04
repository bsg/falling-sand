main: ./src/*.cpp
	clang++ $^ -o main -std=c++20 -Wall -lSDL2 -lSDL2_ttf -O3

index.html: ./src/*.cpp
	em++ $^ -o index.html -std=c++20 -Wall -O3 -s USE_SDL=2 -s USE_SDL_TTF=2 --preload-file ./assets/

asm: ./src/*.cpp
	clang++ $^ -S -mllvm --x86-asm-syntax=intel -std=c++20 -O3

llvm-ir: ./src/*.cpp
	clang++ $^ -S -emit-llvm -std=c++20 -O3