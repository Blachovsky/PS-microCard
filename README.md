[![Firmware build](https://github.com/Blachovsky/PS-microCard/actions/workflows/build.yml/badge.svg)](https://github.com/Blachovsky/PS1-PS2-microSD-Memory-Card/actions/workflows/build.yml)
[![Firmware tests](https://github.com/Blachovsky/PS-microCard/actions/workflows/tests.yml/badge.svg)](https://github.com/Blachovsky/PS1-PS2-microSD-Memory-Card/actions/workflows/tests.yml)

# PS1/PS2 microSD Memory Card

## Szybki start w Visual Studio Code

Do zbudowania i uruchomienia firmware najprościej użyć oficjalnego rozszerzenia
**Raspberry Pi Pico** dla Visual Studio Code. Rozszerzenie pobiera i konfiguruje
Pico SDK, kompilator, CMake, Ninja oraz narzędzia do programowania płytki.

### Wymagania na Linuxie

Oficjalne rozszerzenie obsługuje systemy Linux x64 i arm64. Wymaga Pythona
3.10 lub nowszego, Git 2.28 lub nowszego oraz programu `tar`. Do debugowania
na komputerach x86_64 przydaje się również `gdb-multiarch`.

Na Ubuntu potrzebne narzędzia i biblioteki można zainstalować poleceniem:

```bash
sudo apt update
sudo apt install git python3 tar gdb-multiarch libftdi1-2 libhidapi-hidraw0
```

Aby **Run Project**, **Flash** i debugowanie działały bez `sudo`, zainstaluj
reguły `udev` dla picotool i OpenOCD, a następnie odłącz i ponownie podłącz
płytkę lub Debug Probe. Szczegóły znajdują się w
[dokumentacji rozszerzenia Raspberry Pi Pico](https://github.com/raspberrypi/pico-vscode#requirements-by-os).

### 1. Sklonuj repozytorium razem z submodułami

```console
git clone --recurse-submodules https://github.com/Blachovsky/PS-microCard.git
cd PS-microCard
```

Jeżeli repozytorium zostało wcześniej sklonowane bez submodułów, uzupełnij je:

```console
git submodule update --init --recursive
```

### 2. Otwórz katalog firmware

Otwórz w VS Code katalog `firmware`, a nie katalog główny repozytorium. To w nim
znajdują się główny `CMakeLists.txt` oraz konfiguracja `.vscode`:

```console
code firmware
```

Po otwarciu katalogu zaakceptuj instalację rekomendowanych rozszerzeń. Jeżeli
VS Code nie wyświetli powiadomienia, otwórz panel Extensions i zainstaluj
rozszerzenie **Raspberry Pi Pico** (`raspberry-pi.raspberry-pi-pico`).

### 3. Zaimportuj projekt, jeśli nie został wykryty automatycznie

Zwykle rozszerzenie automatycznie rozpozna projekt i skonfiguruje go po otwarciu
katalogu. Jeżeli tak się nie stanie:

1. Otwórz panel **Raspberry Pi Pico** na pasku bocznym.
2. Wybierz **Import Project**.
3. Jako lokalizację projektu wskaż otwarty katalog `firmware`.
4. Wybierz Pico SDK `2.2.0`, toolchain `14_2_Rel1` oraz płytkę
   **Pico 2 W** (`pico2_w`).
5. Pozostaw integrację z CMake Tools wyłączoną.
6. Zakończ import i poczekaj na pobranie narzędzi oraz konfigurację CMake.

### 4. Zbuduj i uruchom firmware

- **Compile Project** buduje firmware i tworzy między innymi
  `firmware/build/main.uf2`.
- **Run Project** programuje podłączoną płytkę i uruchamia firmware.
- **Debug Project** uruchamia sesję debugowania przez zgodny interfejs SWD.

Przy pierwszym programowaniu przez USB może być konieczne podłączenie płytki
z wciśniętym przyciskiem **BOOTSEL**. Pierwsza konfiguracja może potrwać kilka
minut, ponieważ rozszerzenie pobiera SDK i cały toolchain.

## Testy hostowe

Instrukcja instalacji i uruchamiania testów jednostkowych oraz integracyjnych
znajduje się w [`firmware/tests/README.md`](firmware/tests/README.md).
