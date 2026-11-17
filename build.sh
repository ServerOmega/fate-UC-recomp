#!/usr/bin/env bash
# Constroi o fate-UC-recomp: clona o ps2-native-recomp, aplica os nossos patches e
# roda o port.sh dele, que traduz o jogo e compila.
#
# USO
#   ./build.sh /caminho/para/Fate.iso [/caminho/para/card.ps2]
#   ./build.sh --runtime-only
#
# --runtime-only clona o runtime, aplica os patches e compila o ps2run (o runtime
# e a janela) sem disco. O binario nao contem o jogo: serve para provar a
# toolchain. Traduzir o jogo continua precisando do disco, no primeiro comando.
#
# O QUE ACONTECE
#   1. clona o ps2-native-recomp no commit em que os patches foram escritos
#   2. aplica os patches de patches/ e copia os nossos fontes
#   3. roda scripts/port.sh, que faz o trabalho de verdade: traduz o codigo da EE,
#      compila os microprogramas da VU1, compila os modulos do IOP com o nosso
#      r3000_gen, e faz o build do ps2run
#
# POR QUE NAO CHAMAMOS CMAKE AQUI NO BUILD COM DISCO
# O CMakeLists da raiz nao tem o alvo ps2run: quem o cria e o port.sh, com
# -DPS2_GENERATED e -DPS2_RUNTIME_LIB apontando para o codigo traduzido. Chamar
# cmake na mao gera um build sem alvo, que foi o erro da primeira versao.
# --runtime-only e a excecao: gera um .cpp vazio so com os dois simbolos que o
# host chama, e ai o cmake cria o ps2run de verdade, sem dado de jogo.
#
# Sem o disco nao ha jogo: este repositorio nao contem dado de jogo nenhum, nem
# traduzido nem original.

set -eu

if [ -z "${BASH_VERSION:-}" ]; then
    echo "este script precisa de bash: ./build.sh" >&2
    exit 1
fi

RAIZ="$(cd "$(dirname "$0")" && pwd)"
UPSTREAM_COMMIT="5d1ebbcf133cb3c31df75a294dbe38e30734c1ce"
UPSTREAM_URL="https://github.com/exoteric33/ps2-native-recomp.git"
TRABALHO="${TRABALHO:-$RAIZ/.local}"

# Windows (Git Bash / MSYS): git, cmake e clang sao programas NATIVOS e nao
# traduzem /c/Fate/... . Linux: o caminho do shell ja e o caminho nativo.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    Linux) OS=linux ;;
    *) OS=other ;;
esac

to_native() {
    if [ "$OS" = windows ]; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

usage() {
    echo "uso: $0 /caminho/para/Fate.iso [/caminho/para/card.ps2]" >&2
    echo "     $0 --runtime-only" >&2
    echo "um disco seu, obtido legalmente; o repositorio nao inclui nenhum." >&2
    echo "--runtime-only compila o runner sem traduzir o jogo." >&2
}

RUNTIME_ONLY=0
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --runtime-only) RUNTIME_ONLY=1; shift ;;
esac
if [ "$RUNTIME_ONLY" = 1 ] && [ $# -gt 0 ]; then
    echo "--runtime-only nao recebe disco nem memory card." >&2
    usage
    exit 1
fi

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

if [ "$RUNTIME_ONLY" = 0 ]; then
    if [ -z "$ISO" ]; then
        usage
        exit 1
    fi
    [ -f "$ISO" ] || { echo "disco nao encontrado: $ISO" >&2; exit 1; }
fi

# ---------------------------------------------------------------- dependencias
# O que o runtime pinado (5d1ebbc) realmente usa, lido do CMakeLists e do
# wgpu-native v29 que o scripts/fetch_wgpu.sh baixa:
#   - clang 17+ (GCC nao tem os builtins da VU; o CMakeLists recusa outro compilador)
#   - cmake, ninja, git, python3, curl (o setup.sh baixa o wgpu)
#   - Linux, para compilar a janela: SDL2 e os headers de X11 (SDL_syswm.h)
#   - Linux, em tempo de execucao, o wgpu abre com dlopen: libvulkan.so.1,
#     libwayland-egl.so.1 (Plasma/Wayland) e libX11. O DXC e so Windows.
missing=()
need_cmd() {
    command -v "$1" >/dev/null 2>&1 || missing+=("$2")
}
need_cmd git git
need_cmd cmake cmake
need_cmd ninja ninja
need_cmd curl curl
have_python=0
for candidate in python3 python "py -3"; do
    # shellcheck disable=SC2086
    if $candidate -c 'import sys; sys.exit(sys.version_info < (3,))' >/dev/null 2>&1; then
        have_python=1
        break
    fi
done
[ "$have_python" = 1 ] || missing+=(python3)

have_shared() {
    local soname="$1" found
    if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -F -q "$soname"; then
        return 0
    fi
    found=$(find /usr/lib /usr/lib64 /lib /lib64 -name "$soname" -print -quit 2>/dev/null || true)
    [ -n "$found" ]
}

if [ "$OS" = linux ]; then
    if ! pkg-config --exists sdl2 2>/dev/null && ! compgen -G '/usr/lib*/cmake/SDL2/sdl2-config.cmake' >/dev/null; then
        missing+=("SDL2 (headers)")
    fi
    if ! pkg-config --exists x11 2>/dev/null && [ ! -f /usr/include/X11/Xlib.h ]; then
        missing+=("libX11 (headers)")
    fi
    have_shared libvulkan.so.1 || missing+=("libvulkan.so.1")
    have_shared libwayland-egl.so.1 || missing+=("libwayland-egl.so.1")
    have_shared libX11.so.6 || missing+=("libX11.so.6")
fi

print_install_help() {
    echo "faltam: ${missing[*]}" >&2
    echo >&2
    if [ "$OS" = windows ]; then
        cat >&2 <<'EOF'
Windows (Git Bash). Um comando que cobre a lista:

  winget install Git.Git Kitware.CMake Ninja-build.Ninja Python.Python.3 MartinStorsjo.LLVM-MinGW.UCRT

O clang tambem pode ja estar no PATH, ou em
  %LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW*\llvm-mingw-*\bin\
  ou em "C:\Program Files\LLVM\bin". CXX aponta para um clang 17+ e vence a busca.
SDL2, wgpu-native e o DXC (so Direct3D 12) o proprio build baixa. Nao ha passo
de apt/pacman.
EOF
        return
    fi
    cat >&2 <<'EOF'
Linux. O runtime pinado compila com clang (nao GCC), SDL2 e X11, e na hora de
jogar abre libvulkan.so.1 e libwayland-egl.so.1 (KDE Plasma incluso). Instale a
linha do seu sistema; um driver Vulkan entra na segunda linha.

  Arch Linux
    sudo pacman -S --needed clang cmake ninja git python curl sdl2 libx11 wayland vulkan-icd-loader
    driver: vulkan-radeon, vulkan-intel, nvidia-utils, ou vulkan-swrast (lavapipe)

  Debian / Ubuntu
    sudo apt install clang cmake ninja-build git python3 curl libsdl2-dev libx11-dev libwayland-dev libvulkan1
    driver: mesa-vulkan-drivers, ou o pacote da NVIDIA

  Fedora
    sudo dnf install clang cmake ninja-build git python3 curl SDL2-devel libX11-devel wayland-devel vulkan-loader
    driver: mesa-vulkan-drivers, ou o pacote da NVIDIA

Se o clang existe mas o teste de <format> falha, a libstdc++ que ele achou e
velha demais (no Ubuntu o clang as vezes escolhe um GCC mais novo do que o dos
headers instalados). Instale a libstdc++ desse GCC, ou a libc++:
  Arch: libc++    Debian: libc++-dev libc++abi-dev    Fedora: libcxx-devel
EOF
}

# --------------------------------------------------------------- compilador
# CXX do ambiente vence e nao e substituido. Sem ele: PATH (clang++ mais novo
# primeiro, como o scripts/cxx.sh do upstream), e no Windows as pastas comuns
# do LLVM-MinGW / WinGet e do LLVM em Program Files. Nenhum usuario fixo.
mkdir -p "$TRABALHO"
probe_err="$TRABALHO/probe.err"
: > "$probe_err"

probe_one() {
    local cand="$1" resolved src exe stdlib
    [ -n "$cand" ] || return 1
    if [ -x "$cand" ]; then
        resolved="$cand"
    else
        resolved=$(command -v "$cand" 2>/dev/null || true)
        [ -n "$resolved" ] && [ -x "$resolved" ] || return 1
    fi
    if [ "$OS" = windows ] && [ -f "${resolved}.exe" ]; then
        resolved="${resolved}.exe"
    fi
    case " $probed " in
        *" $resolved "*) return 1 ;;
    esac
    probed="$probed $resolved "
    src=$(to_native "$TRABALHO/probe.cpp")
    if [ "$OS" = windows ]; then
        exe=$(to_native "$TRABALHO/probe.exe")
    else
        exe=$(to_native "$TRABALHO/probe.bin")
    fi
    cat > "$TRABALHO/probe.cpp" << 'PROBE'
#ifndef __clang__
#error not clang
#endif
#include <format>
typedef int v4i __attribute__((vector_size(16)));
int main() {
  v4i a = {1, 2, 3, 4};
  return __builtin_reduce_or(__builtin_elementwise_min(a, a)) != 7 || std::format("{}", 1).size() != 1;
}
PROBE
    : > "$probe_err"
    for stdlib in "" -stdlib=libc++; do
        # shellcheck disable=SC2086
        if { echo "== $resolved ${stdlib:-<libstdc++>}"; "$resolved" -std=c++20 $stdlib "$src" -o "$exe"; } >>"$probe_err" 2>&1; then
            FOUND_CXX="$resolved"
            rm -f "$TRABALHO/probe.bin" "$TRABALHO/probe.exe"
            return 0
        fi
    done
    return 1
}

FOUND_CXX=""
probed=" "
if [ -n "${CXX:-}" ]; then
    if ! probe_one "$CXX"; then
        echo "CXX=$CXX nao compilou o teste (clang 17+ com <format> e os builtins da VU; GCC nao serve)." >&2
        echo "ultima saida do compilador:" >&2
        tail -20 "$probe_err" >&2 || true
        missing+=("clang (CXX nao serve)")
    fi
else
    path_candidates=()
    if compgen -c clang++- >/dev/null 2>&1; then
        while IFS= read -r name; do
            [ -n "$name" ] && path_candidates+=("$name")
        done < <(compgen -c clang++- | grep -E '^clang\+\+-[0-9]+$' | sort -t- -k2 -rn | uniq || true)
    fi
    path_candidates+=(clang++ clang++.exe clang clang.exe)
    for cand in "${path_candidates[@]}"; do
        probe_one "$cand" && break
    done
    if [ -z "$FOUND_CXX" ] && [ "$OS" = windows ]; then
        win_roots=()
        if [ -n "${LOCALAPPDATA:-}" ]; then
            win_roots+=("$(cygpath -u "$LOCALAPPDATA")")
        fi
        if [ -n "${USERPROFILE:-}" ]; then
            win_roots+=("$(cygpath -u "$USERPROFILE")/AppData/Local")
        fi
        win_roots+=("$HOME/AppData/Local")
        seen_roots=" "
        shopt -s nullglob
        for root in "${win_roots[@]}"; do
            [ -n "$root" ] && [ -d "$root" ] || continue
            case " $seen_roots " in *" $root "*) continue ;; esac
            seen_roots="$seen_roots $root "
            for f in \
                "$root"/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW*/llvm-mingw-*/bin/clang++.exe \
                "$root"/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW*/llvm-mingw-*/bin/clang.exe
            do
                [ -f "$f" ] || continue
                probe_one "$f" && break
            done
            [ -n "$FOUND_CXX" ] && break
        done
        win_bins=("/c/Program Files/LLVM/bin" "/c/Program Files/LLVM-MinGW/bin" "/c/llvm-mingw/bin")
        if [ -n "${PROGRAMFILES:-}" ]; then
            win_bins+=("$(cygpath -u "$PROGRAMFILES")/LLVM/bin")
        fi
        for bin in "${win_bins[@]}"; do
            [ -n "$FOUND_CXX" ] && break
            [ -f "$bin/clang++.exe" ] && probe_one "$bin/clang++.exe" && break
            [ -f "$bin/clang.exe" ] && probe_one "$bin/clang.exe" && break
        done
        shopt -u nullglob
    fi
    if [ -z "$FOUND_CXX" ]; then
        missing+=("clang 17+ com <format>")
        if [ -s "$probe_err" ]; then
            echo "nenhum clang passou no teste. ultima saida:" >&2
            tail -20 "$probe_err" >&2 || true
            echo >&2
        fi
    fi
fi

if [ ${#missing[@]} -gt 0 ]; then
    print_install_help
    exit 1
fi

CXX="$FOUND_CXX"
sibling_cc() {
    local dir base cc
    dir=$(dirname "$1")
    base=$(basename "$1")
    case "$base" in
        clang++*) cc="clang${base#clang++}" ;;
        clang*) cc="$base" ;;
        *) cc="" ;;
    esac
    if [ -n "$cc" ] && [ -x "$dir/$cc" ]; then
        printf '%s\n' "$dir/$cc"
    elif [ -x "$dir/clang.exe" ]; then
        printf '%s\n' "$dir/clang.exe"
    elif [ -x "$dir/clang" ]; then
        printf '%s\n' "$dir/clang"
    fi
}
if [ -z "${CC:-}" ]; then
    CC=$(sibling_cc "$CXX" || true)
fi
if [ "$OS" = windows ]; then
    CXX=$(cygpath -m "$CXX")
    if [ -n "${CC:-}" ]; then
        CC=$(cygpath -m "$CC")
    fi
fi
export CXX
if [ -n "${CC:-}" ]; then
    export CC
fi
echo "   compilador: $CXX"

if [ "$OS" = linux ]; then
    have_icd=0
    icd_dirs=(/usr/share/vulkan/icd.d /usr/local/share/vulkan/icd.d /etc/vulkan/icd.d "${XDG_DATA_HOME:-$HOME/.local/share}/vulkan/icd.d")
    shopt -s nullglob
    for d in "${icd_dirs[@]}"; do
        for f in "$d"/*.json; do
            have_icd=1
            break 2
        done
    done
    shopt -u nullglob
    if [ "$have_icd" = 0 ]; then
        cat >&2 <<'EOF'
aviso: nenhum driver Vulkan (ICD) encontrado. O ps2run compila mesmo assim,
mas a janela nao desenha ate existir um .json em /usr/share/vulkan/icd.d.
  Arch:    vulkan-radeon, vulkan-intel, nvidia-utils, ou vulkan-swrast
  Debian:  mesa-vulkan-drivers (ou o driver da NVIDIA)
  Fedora:  mesa-vulkan-drivers (ou o driver da NVIDIA)
EOF
    fi
fi

# ---------------------------------------------------------------- 1. upstream
TRABALHO_NATIVE=$(to_native "$TRABALHO")
if [ ! -d "$TRABALHO/upstream" ]; then
    echo "== clonando o ps2-native-recomp"
    # Sem --depth: precisamos do commit pinado, que nao e o HEAD, e um clone raso
    # so traz o HEAD.
    git clone "$UPSTREAM_URL" "$TRABALHO_NATIVE/upstream"
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
# Os patches sao diff de contexto em LF. No Windows, com core.autocrlf=true (o
# padrao do Git for Windows), o clone fica em CRLF e o `git apply` normaliza os
# dois lados. Forcar false aqui QUEBRA a aplicacao: foi tentado. No Linux o
# blob ja e LF e o autocrlf padrao e false, entao o apply casa sem conversao.
# So a tolerancia de espaco em branco precisa ser afrouxada.
git config apply.whitespace nowarn

echo "== aplicando os patches"
for p in "$RAIZ"/patches/*.patch; do
    nome="$(basename "$p")"
    # caminho nativo: o git do Windows nao traduz /c/...
    p_native="$(to_native "$p")"
    if git apply --check "$p_native" 2>/dev/null; then
        git apply "$p_native" && echo "  ok    $nome"
    elif git apply --reverse --check "$p_native" 2>/dev/null; then
        echo "  ja    $nome"
    else
        echo "  FALHA $nome" >&2
        git apply --check "$p_native" 2>&1 | head -3 >&2
        echo "  o upstream mudou demais para este patch; veja docs/BUILD.md" >&2
        exit 1
    fi
done

# ------------------------------------------------------------- 3. nossos fontes
echo "== copiando os fontes proprios"
cp -r "$RAIZ/recomp/."   "$TRABALHO/upstream/recomp/"
cp -r "$RAIZ/runtime/."  "$TRABALHO/upstream/runtime/"
cp "$RAIZ/scripts/frame_report.py" "$TRABALHO/upstream/scripts/"

publicar() {
    local dir="$1"
    mkdir -p "$dir"
    if [ ! -f "$dir/keys.ini" ]; then
        cp "$RAIZ/config/keys.ini" "$dir/keys.ini"
        echo "== keys.ini copiado para $dir"
    fi
    if [ "$OS" = windows ]; then
        cp "$RAIZ/play.bat" "$dir/play.bat"
    else
        cp "$RAIZ/play.sh" "$dir/play.sh"
        chmod +x "$dir/play.sh"
    fi
    if [ -n "${ISO:-}" ] && [ -f "$ISO" ]; then
        local nome
        nome=$(basename "$ISO")
        if [ ! -e "$dir/$nome" ]; then
            ln -s "$ISO" "$dir/$nome" 2>/dev/null || \
                echo "aviso: nao liguei o disco em $dir (passe o caminho ao launcher)." >&2
        fi
    fi
    if [ -d "$TRABALHO/upstream/games/fate" ] && [ ! -f "$TRABALHO/upstream/games/fate/keys.ini" ]; then
        cp "$RAIZ/config/keys.ini" "$TRABALHO/upstream/games/fate/keys.ini"
    fi
}

if [ "$RUNTIME_ONLY" = 1 ]; then
    echo "== runtime sem disco: wgpu, ferramentas e o ps2run (sem o jogo)"
    ./scripts/setup.sh --native-only
    stub="$TRABALHO/upstream/generated/host-stub"
    mkdir -p "$stub"
    cat > "$stub/register_stub.cpp" << 'EOF'
// build.sh --runtime-only: nao ha disco, entao nao ha codigo traduzido.
// host/main.cpp chama estes dois simbolos; o port.sh troca esta pasta pelos
// .cpp gerados do jogo.
void register_generated_code() {}
void register_generated_vu() {}
EOF
    ROOT=$(to_native "$TRABALHO/upstream")
    export ROOT
    cxx_flags=()
    # shellcheck disable=SC1091
    . "$TRABALHO/upstream/scripts/cxx.sh"
    host_dir="$TRABALHO/upstream/build/host-only"
    cmake -S "$ROOT" -B "$(to_native "$host_dir")" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "${cxx_flags[@]}" -DPS2_GENERATED="$(to_native "$stub")"
    if [ -n "${PS2_JOBS:-}" ]; then
        case "$PS2_JOBS" in
            ''|*[!0-9]*|0*) echo "PS2_JOBS=$PS2_JOBS: esperado um inteiro positivo" >&2; exit 2 ;;
        esac
        ninja -C "$(to_native "$host_dir")" -j "$PS2_JOBS" ps2run
    else
        ninja -C "$(to_native "$host_dir")" ps2run
    fi
    if ! grep -q 'window.cpp' "$host_dir/build.ninja"; then
        echo "o ps2run linkou sem a janela: o cmake nao achou o SDL2." >&2
        exit 1
    fi
    publicar "$host_dir"
    echo
    echo "pronto (sem disco: este binario nao contem o jogo)."
    if [ "$OS" = windows ]; then
        echo "  runner: $(to_native "$host_dir/ps2run.exe")"
        echo "  jogue de verdade com: $0 caminho/para/Fate.iso"
    else
        echo "  runner: $host_dir/ps2run"
        echo "  launcher (vai pedir o disco): $host_dir/play.sh"
        echo "  jogue de verdade com: $0 caminho/para/Fate.iso"
    fi
    exit 0
fi

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
# port.sh <nome> <disco> [opcoes]. O disco e o segundo argumento posicional.
# --native-only pula o build web (Emscripten), que no Windows ainda quebra com
# caminho POSIX e nao e necessario para jogar.
if [ -n "${CC:-}" ]; then
    CXX="$CXX" CC="$CC" ./scripts/port.sh fate games/fate/disc.iso --native-only
else
    CXX="$CXX" ./scripts/port.sh fate games/fate/disc.iso --native-only
fi

out="$TRABALHO/upstream/build/native-fate"
publicar "$out"
echo
echo "pronto."
if [ "$OS" = windows ]; then
    echo "  runner: $(to_native "$out/ps2run.exe")"
    echo "  jogue:  $(to_native "$out/play.bat")"
else
    echo "  runner: $out/ps2run"
    echo "  jogue:  $out/play.sh"
fi
echo "  o launcher acha um .iso/.bin/.img ao lado dele, ou recebe o caminho."
