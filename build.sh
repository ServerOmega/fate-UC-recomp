#!/usr/bin/env bash
# Constroi o fate-UC-recomp: clona o ps2-native-recomp, aplica os nossos patches e
# roda o port.sh dele, que traduz o jogo e compila.
#
# USO
#   ./build.sh /caminho/para/Fate.iso [/caminho/para/card.ps2]
#
# O QUE ACONTECE
#   1. clona o ps2-native-recomp no commit em que os patches foram escritos
#   2. aplica os patches de patches/ e copia os nossos fontes
#   3. roda scripts/port.sh, que faz o trabalho de verdade: traduz o codigo da EE,
#      compila os microprogramas da VU1, compila os modulos do IOP com o nosso
#      r3000_gen, e faz o build do ps2run
#
# POR QUE NAO CHAMAMOS CMAKE AQUI
# O CMakeLists da raiz nao tem o alvo ps2run: quem o cria e o port.sh, com
# -DPS2_GENERATED e -DPS2_RUNTIME_LIB apontando para o codigo traduzido. Chamar
# cmake na mao gera um build sem alvo, que foi o erro da primeira versao.
#
# Sem o disco nao ha jogo: este repositorio nao contem dado de jogo nenhum, nem
# traduzido nem original.

set -eu

RAIZ="$(cd "$(dirname "$0")" && pwd)"
# O git e o cmake do Windows sao programas NATIVOS e nao traduzem caminho POSIX:
# passar /c/Fate/... para eles cria C:\c\Fate\... (aconteceu). Todo caminho que
# sai daqui para um programa nativo tem que ser C:/...
RAIZ_WIN="$(cygpath -m "$RAIZ" 2>/dev/null || echo "$RAIZ")"
UPSTREAM_COMMIT="5d1ebbcf133cb3c31df75a294dbe38e30734c1ce"
UPSTREAM_URL="https://github.com/exoteric33/ps2-native-recomp.git"
TRABALHO="${TRABALHO:-$RAIZ/.local}"
TRABALHO_WIN="$(cygpath -m "$TRABALHO" 2>/dev/null || echo "$TRABALHO")"
ISO="${1:-}"
CARD="${2:-}"
# O script passa a rodar dentro de .local/upstream, entao um caminho relativo dado
# pelo usuario (iso/Fate-UC.iso) tem que ser resolvido AGORA, contra a pasta do
# projeto. Sem isso, "cd" quebra o caminho e o port.sh nao acha o disco.
case "$ISO" in
    "") ;;
    /*|[A-Za-z]:[/\\]*) ;;
    *) ISO="$RAIZ/$ISO" ;;
esac
if [ -n "$CARD" ]; then
    case "$CARD" in
        /*|[A-Za-z]:[/\\]*) ;;
        *) CARD="$RAIZ/$CARD" ;;
    esac
fi

if [ -z "$ISO" ]; then
    echo "uso: $0 /caminho/para/Fate.iso [/caminho/para/card.ps2]" >&2
    echo "um disco seu, obtido legalmente; o repositorio nao inclui nenhum." >&2
    exit 1
fi
[ -f "$ISO" ] || { echo "disco nao encontrado: $ISO" >&2; exit 1; }
command -v git     >/dev/null || { echo "falta git" >&2; exit 1; }
command -v cmake   >/dev/null || { echo "falta cmake" >&2; exit 1; }
command -v ninja   >/dev/null || { echo "falta ninja" >&2; exit 1; }
command -v clang++ >/dev/null || { echo "falta clang++ (o codigo vetorial do upstream usa builtins do clang)" >&2; exit 1; }

mkdir -p "$TRABALHO"

# ---------------------------------------------------------------- 1. upstream
if [ ! -d "$TRABALHO/upstream" ]; then
    echo "== clonando o ps2-native-recomp"
    # Sem --depth: precisamos do commit pinado, que nao e o HEAD, e um clone raso
    # so traz o HEAD.
    git clone "$UPSTREAM_URL" "$TRABALHO_WIN/upstream"
fi
cd "$TRABALHO/upstream"
if ! git checkout --quiet "$UPSTREAM_COMMIT" 2>/dev/null; then
    echo "  buscando o commit $UPSTREAM_COMMIT"
    git fetch --unshallow --quiet 2>/dev/null || git fetch --all --quiet
    git checkout --quiet "$UPSTREAM_COMMIT" || {
        echo "commit $UPSTREAM_COMMIT nao encontrado; usando o HEAD" >&2
    }
fi
echo "== upstream em $(git log --oneline -1)"

# ------------------------------------------------------------------ 2. patches
# Os patches sao diff de contexto em LF e o clone do upstream esta em CRLF. Com o
# core.autocrlf desta maquina em true (o padrao do Git for Windows) o `git apply`
# normaliza os dois lados e o patch casa. Forcar false aqui QUEBRA a aplicacao:
# foi tentado, e o git passou a reclamar de "patch does not apply". So a
# tolerancia de espaco em branco precisa ser afrouxada.
git config apply.whitespace nowarn

echo "== aplicando os patches"
for p in "$RAIZ"/patches/*.patch; do
    nome="$(basename "$p")"
    # caminho nativo: o git nao traduz /c/...
    p_win="$(cygpath -m "$p" 2>/dev/null || echo "$p")"
    if git apply --check "$p_win" 2>/dev/null; then
        git apply "$p_win" && echo "  ok    $nome"
    elif git apply --reverse --check "$p_win" 2>/dev/null; then
        echo "  ja    $nome"
    else
        echo "  FALHA $nome" >&2
        git apply --check "$p_win" 2>&1 | head -3 >&2
        echo "  o upstream mudou demais para este patch; veja docs/BUILD.md" >&2
        exit 1
    fi
done

# ------------------------------------------------------------- 3. nossos fontes
echo "== copiando os fontes proprios"
cp -r "$RAIZ/recomp/."   "$TRABALHO/upstream/recomp/"
cp -r "$RAIZ/runtime/."  "$TRABALHO/upstream/runtime/"
cp "$RAIZ/scripts/frame_report.py" "$TRABALHO/upstream/scripts/"

# -------------------------------------------------------- 4. o jogo no lugar
mkdir -p games/fate
if [ ! -e "games/fate/disc.iso" ]; then
    ln -s "$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")" games/fate/disc.iso 2>/dev/null || \
        cp "$ISO" games/fate/disc.iso
fi
if [ -n "$CARD" ] && [ -f "$CARD" ] && [ ! -e "games/fate/card.ps2" ]; then
    ln -s "$(cd "$(dirname "$CARD")" && pwd)/$(basename "$CARD")" games/fate/card.ps2 2>/dev/null || \
        cp "$CARD" games/fate/card.ps2
fi

# --------------------------------------------- 5. port.sh faz o trabalho
echo "== traduzindo e compilando (pode levar de 20 a 40 minutos)"
# port.sh <nome> <disco> [opcoes] - o disco e o segundo argumento posicional, nao
# uma opcao --iso. O nome vira o de build/native-<nome>/.
#
# O COMPILADOR. O scripts/cxx.sh do upstream procura um clang com <format> e nao
# aceita GCC. O clang que funciona nesta maquina e o LLVM-MinGW do WinGet, que nao
# esta no PATH; por isso procuramos os candidatos na ordem e damos o caminho
# completo ao port.sh. Se CXX ja vier do ambiente, ele vence.
if [ -z "${CXX:-}" ]; then
    for cand in \
        "C:/Users/azure/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/llvm-mingw-20261006-ucrt-x86_64/bin/clang.exe" \
        "$(command -v clang 2>/dev/null)" \
        "$(command -v clang-19 2>/dev/null)" \
        "$(command -v clang-18 2>/dev/null)" \
        "$(command -v clang-17 2>/dev/null)"
    do
        [ -n "$cand" ] && [ -x "$cand" ] || continue
        # O clang do Windows e um programa NATIVO: nao le /c/... . O probe tem que
        # usar caminho C:/..., senao ele falha com "no such file" e um compilador
        # bom e descartado (foi o que aconteceu antes).
        probe_src="$TRABALHO_WIN/probe.cpp"
        probe_exe="$TRABALHO_WIN/probe.exe"
        printf '#include <format>\nint main(){return 0;}\n' > "$TRABALHO/probe.cpp" 2>/dev/null || continue
        if "$cand" -std=c++20 "$probe_src" -o "$probe_exe" 2>/dev/null; then
            CXX="$cand"
            echo "   compilador: $CXX"
            break
        fi
    done
    if [ -z "${CXX:-}" ]; then
        echo "nenhum clang com <format> encontrado. Instale o LLVM-MinGW (winget install MartinStorsjo.LLVM-MinGW.UCRT)" >&2
        echo "ou aponte CXX para um clang 17+." >&2
        exit 1
    fi
fi

CXX="$CXX" CC="${CC:-$(dirname "$CXX")/clang.exe}" \
    ./scripts/port.sh fate games/fate/disc.iso --native-only 2>&1 | grep -vE "^\s*$" || true

echo
echo "pronto."
echo "  runner: build/native-fate/ps2run.exe"
echo "  jogue:  $RAIZ/play-fate.bat"
