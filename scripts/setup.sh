#!/usr/bin/env bash
# Initialisiert alle Submodule nach einem frischen Clone und baut die Libraries.
#
#   git clone <repo-url>          (ohne --recursive!)
#   cd <repo> && ./scripts/setup.sh
#
# Warum nicht einfach "git submodule update --init --recursive"?
# Das wuerde auch die Submodule von ref/DaisyExamples (libDaisy, DaisySP, stmlib
# inkl. CMSIS/HAL) ein zweites Mal herunterladen. Die Beispiele bauen wir stattdessen
# gegen lib/ (siehe README).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== lib/: libDaisy, DaisySP, stmlib"
git submodule update --init lib/libDaisy lib/DaisySP lib/stmlib

# CMSIS, HAL-Treiber, USB-Middleware: flach reicht, deren Historie braucht man nicht
git -C lib/libDaisy submodule update --init --recursive --depth 1
# DaisySP-LGPL (ReverbSc, MoogLadder, ...)
git -C lib/DaisySP submodule update --init --recursive

echo "== ref/DaisyExamples (flach, ohne eigene Submodule)"
git submodule update --init --depth 1 ref/DaisyExamples

if command -v make >/dev/null && command -v arm-none-eabi-gcc >/dev/null; then
    echo "== Libraries bauen"
    make libs -j"$(nproc 2>/dev/null || echo 4)"
else
    echo "make/arm-none-eabi-gcc nicht gefunden - DaisyToolchain installieren, dann: make libs"
fi
