# PS microCard

[![Firmware build](https://github.com/Blachovsky/PS-microCard/actions/workflows/build.yml/badge.svg)](https://github.com/Blachovsky/PS-microCard/actions/workflows/build.yml)
[![Firmware tests](https://github.com/Blachovsky/PS-microCard/actions/workflows/tests.yml/badge.svg)](https://github.com/Blachovsky/PS-microCard/actions/workflows/tests.yml)

## Budowanie firmware w VS Code

Projekt korzysta z oficjalnego rozszerzenia **Raspberry Pi Pico**. Rozszerzenie
instaluje i konfiguruje Pico SDK 2.2.0, toolchain, CMake oraz Ninja. Zależność
SDIO/FatFs jest pobierana automatycznie przez CMake podczas pierwszej
konfiguracji.

1. Sklonuj repozytorium zwykłym poleceniem:

   ```sh
   git clone https://github.com/Blachovsky/PS-microCard.git
   ```

2. Zainstaluj w VS Code rozszerzenie
   [Raspberry Pi Pico](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico).
3. Uruchom polecenie `Raspberry Pi Pico: Import Project`.
4. Jako katalog projektu wskaż `firmware`.
5. Wybierz płytkę `Pico 2 W`, Pico SDK `2.2.0` i pozostaw integrację
   z CMake Tools wyłączoną.
6. Po zakończeniu konfiguracji wybierz `Compile Project`.

Wynikowy plik UF2 znajduje się w `firmware/build/main.uf2`.

## Budowanie z terminala

Po ustawieniu `PICO_SDK_PATH` można użyć standardowych poleceń CMake:

```sh
cmake -S firmware -B firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build firmware/build --parallel
```
