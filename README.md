# Mangoneado — Simulación del sistema de etiquetado

Repositorio con una simulación en C de la línea de empacado/etiquetado:
- `vision` genera posiciones de mangos y las envía por TCP.
- `robots` recibe las posiciones y simula robots (pthread) etiquetando.

Funciona en:
- Ubuntu (gcc, pthreads, sockets BSD)
- Windows MSYS2/MinGW (WinSock2 + pthreads-w32)

## Estructura
mangoneado/
├── README.md
├── .gitignore
├── Makefile
├── src/
│   ├── platform.h
│   ├── common.h
│   ├── common.c
│   ├── vision.c
│   └── robots.c
├── scripts/
│   ├── run_simulation.sh
│   └── run_simulation.bat
└── tests/
    └── test_cases.md

## Compilación

### En Ubuntu
bash
sudo apt update
sudo apt install build-essential
make

### En MSYS2 / MinGW (64-bit)
bash
Copy code
pacman -Syu
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pthreads
make

### Ejecución (ejemplo)
Terminal A:
bash
Copy code
./build/robots 9000 4 10 30 200 200 0.05

Terminal B:
bash
Copy code
./build/vision 127.0.0.1 9000 15 30 1234

### Scripts
scripts/run_simulation.sh — Linux

scripts/run_simulation.bat — Windows