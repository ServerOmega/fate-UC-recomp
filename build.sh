#!/usr/bin/env bash
# Constroi o fate-UC-recomp: clona o ps2-native-recomp, aplica os nossos patches,
# gera o codigo nativo do jogo e compila.
#
# USO
#   ./build.sh /caminho/para/Fate.iso [/caminho/para/card.ps2]
#
# O que acontece, em ordem:
#   1. clona o ps2-native-recomp no commit em que os patches foram escritos
#   2. aplica os 11 patches de patches/
#   3. copia os nossos fontes (r3000, gen_iop, tuned_defaults) para a arvore
#   4. compila o gerador do IOP e gera os 20 modulos a partir do IRX do disco
#   5. build normal do cmake/ninja
#
# Sem o disco nao ha jogo: este repositorio nao contem dado de jogo nenhum, nem
# traduzido nem original.

set -eu

RAIZ="$(cd "$(dirname "$0")" && pwd)"
UPSTREAM_COMMIT="5d1ebbcf133cb3c31df75a294dbe38e30734c1ce"
UPSTREAM_URL="https://github.com/exoteric33/ps2-native-recomp.git"
TRABALHO="${TRABALHO:-$RAIZ/.local}"
ISO="${1:-}"
CARD="${2:-}"

if [ -z "$ISO" ]; then
    echo "uso: $0 /caminho/para/Fate.iso [/caminho/para/card.ps2]" >&2
    echo "um disco seu, obtido legalmente; o repositorio nao inclui nenhum." >&2
    exit 1
fi
[ -f "$ISO" ] || { echo "disco nao encontrado: $ISO" >&2; exit 1; }

mkdir -p "$TRABALHO"

# ---------------------------------------------------------------- 1. upstream
if [ ! -d "$TRABALHO/upstream" ]; then
    echo "== clonando o ps2-native-recomp"
    git clone "$UPSTREAM_URL" "$TRABALHO/upstream"
fi
cd "$TRABALHO/upstream"
git fetch --all --quiet || true
git checkout --quiet "$UPSTREAM_COMMIT" || {
    echo "commit $UPSTREAM_COMMIT nao encontrado; usando o main atual" >&2
    git checkout --quiet main
}

# ------------------------------------------------------------------ 2. patches
echo "== aplicando os patches"
for p in "$RAIZ"/patches/*.patch; do
    nome="$(basename "$p")"
    if git apply --check "$p" 2>/dev/null; then
        git apply "$p"
        echo "  ok   $nome"
    elif git apply --reverse --check "$p" 2>/dev/null; then
        echo "  ja   $nome"
    else
        echo "  FALHA $nome" >&2
        echo "  o upstream mudou demais para este patch; veja docs/BUILD.md" >&2
        exit 1
    fi
done

# ------------------------------------------------------------- 3. nossos fontes
echo "== copiando os fontes proprios"
cp -r "$RAIZ/recomp/." "$TRABALHO/upstream/recomp/"
cp -r "$RAIZ/runtime/." "$TRABALHO/upstream/runtime/"
cp "$RAIZ/scripts/frame_report.py" "$TRABALHO/upstream/scripts/"

# ------------------------------------------------------------- 4. codigo do IOP
# O gerador le os IRX que o jogo traz no disco; nada de IRX fica no repositorio.
echo "== gerando o codigo nativo do IOP"
mkdir -p games/fate
[ -f "games/fate/disc.iso" ] || ln -sf "$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")" games/fate/disc.iso
if [ -n "$CARD" ] && [ -f "$CARD" ]; then
    ln -sf "$(cd "$(dirname "$CARD")" && pwd)/$(basename "$CARD")" games/fate/card.ps2
fi

CXX="${CXX:-clang++}"
"$CXX" -std=gnu++20 -I. -Iruntime/include -Iruntime/src -O2 \
    -o "$TRABALHO/gen_iop" \
    recomp/r3000_gen.cpp recomp/gen_iop.cpp recomp/elf.cpp recomp/r3000.cpp

mkdir -p "$TRABALHO/iop-modules" "$TRABALHO/generated-iop"
if [ ! -d "$TRABALHO/iop-modules" ] || [ -z "$(ls -A "$TRABALHO/iop-modules" 2>/dev/null)" ]; then
    echo "  extraindo os IRX do disco (o jogo os traz dentro do IOPRP310.IMG)"
    echo "  veja docs/BUILD.md, secao 'Os modulos do IOP'" >&2
fi
"$TRABALHO/gen_iop" "$TRABALHO/iop-modules" "$TRABALHO/generated-iop"

# ----------------------------------------------------------------- 5. build
echo "== compilando"
cmake -S . -B "$TRABALHO/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C "$TRABALHO/build" ps2run

echo
echo "pronto: $TRABALHO/build/ps2run.exe"
echo "rode com: $RAIZ/play-fate.bat  (ou veja README.md)"
