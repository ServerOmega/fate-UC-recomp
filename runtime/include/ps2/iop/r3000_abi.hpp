#pragma once

// ABI do codigo gerado do IOP (R3000).
//
// O tradutor emite C++ que chama estas funcoes. Elas existem para que o codigo
// gerado nao precise saber COMO a memoria do IOP funciona - se le de RAM, de
// registrador de hardware ou de um handler de HLE. Essa separacao e a mesma que
// o runtime da EE usa (ps2/ee_live_abi.hpp).
//
// IMPORTANTE: as leituras e escritas passam pelas funcoes do runtime
// (ps2::iop::read8/write8), nao por acesso direto ao array g_ram. O IOP tem
// registradores mapeados em memoria (0x1F80xxxx) e um handler de HLE que
// intercepta enderecos especificos; ir direto na RAM quebraria os dois.

#include <cstdint>

// A ABI e publica (o codigo gerado a inclui), entao NAO pode depender de
// headers internos de runtime/src. O que ela precisa do IOP esta declarado
// aqui, com o mesmo layout usado em runtime/src/iop/iop.hpp. O runtime confere
// o layout com static_assert em r3000_native.cpp.
#ifndef PS2_IOP_TIPOS_DECLARADOS
// Definidos aqui quando o codigo gerado compila sozinho (so runtime/include).
// Quando o runtime interno (runtime/src/iop/iop.hpp) ja declarou, ele define
// PS2_IOP_TIPOS_DECLARADOS antes e este bloco e pulado - sem redefinicao.
#define PS2_IOP_TIPOS_DECLARADOS 1

namespace ps2::iop {

inline constexpr std::uint32_t kRamSize = 2u << 20;

struct Cpu {
    std::uint32_t r[32];
    std::uint32_t hi, lo;
    std::uint32_t pc, npc;
    std::uint32_t status, cause, epc;
};

extern Cpu g_cpu;

// Definidas em runtime/src/iop/io.cpp; tratam RAM, registradores mapeados e HLE.
std::uint8_t read8(std::uint32_t a);
std::uint16_t read16(std::uint32_t a);
std::uint32_t read32(std::uint32_t a);
void write8(std::uint32_t a, std::uint8_t v);
void write16(std::uint32_t a, std::uint16_t v);
void write32(std::uint32_t a, std::uint32_t v);

}  // namespace ps2::iop

#endif  // PS2_IOP_TIPOS_DECLARADOS

namespace ps2::iop::r3000 {

using ps2::iop::Cpu;

// Sentinela de fim de execucao, igual ao dispatch da EE.
inline constexpr std::uint32_t kUnwind = 0x80000000u;
// Profundidade maxima de chamadas traduzidas antes de desistir.
inline constexpr std::uint32_t kMaxDepth = 64;

// Uma entrada da tabela: endereco do codigo e a funcao que o executa.
struct Entry {
    std::uint32_t pc;
    void (*fn)(Cpu &, std::uint32_t);
    // Quantas instrucoes o bloco gerado executa antes de devolver o controle.
    // A ponte precisa disto para cobrar ciclos: devolver sempre 1 fazia o IOP
    // rodar ~28x adiantado (737 mil "ciclos" por quadro em vez de 26 mil, som
    // parado, sync_iop de 58 ms). E a contagem estatica de instrucoes entre este
    // pc e o proximo ponto de devolucao.
    std::uint16_t ciclos;
};

// ---------------------------------------------------------------- memoria
// Passam pelo runtime, que trata RAM, registradores de hardware e HLE.
inline std::uint8_t ld8(std::uint32_t a) { return ps2::iop::read8(a); }
inline std::uint16_t ld16(std::uint32_t a) { return ps2::iop::read16(a); }
inline std::uint32_t ld32(std::uint32_t a) { return ps2::iop::read32(a); }
inline void st8(std::uint32_t a, std::uint8_t v) { ps2::iop::write8(a, v); }
inline void st16(std::uint32_t a, std::uint16_t v) { ps2::iop::write16(a, v); }
inline void st32(std::uint32_t a, std::uint32_t v) { ps2::iop::write32(a, v); }

// LWL/LWR: carregam parte de uma palavra alinhada, conforme o byte address.
// R3000: LWL pega os bytes altos, LWR os baixos, e a combinacao forma a palavra.
inline std::uint32_t lwl(std::uint32_t a, std::uint32_t atual) {
    const std::uint32_t alinhado = a & ~3u;
    const std::uint32_t w = ps2::iop::read32(alinhado);
    switch (a & 3u) {
    case 0: return (atual & 0x00FFFFFFu) | (w << 24);
    case 1: return (atual & 0x0000FFFFu) | (w << 16);
    case 2: return (atual & 0x000000FFu) | (w << 8);
    default: return w;
    }
}

inline std::uint32_t lwr(std::uint32_t a, std::uint32_t atual) {
    const std::uint32_t alinhado = a & ~3u;
    const std::uint32_t w = ps2::iop::read32(alinhado);
    switch (a & 3u) {
    case 0: return w;
    case 1: return (atual & 0xFF000000u) | (w >> 8);
    case 2: return (atual & 0xFFFF0000u) | (w >> 16);
    default: return (atual & 0xFFFFFF00u) | (w >> 24);
    }
}

inline void swl(std::uint32_t a, std::uint32_t v) {
    const std::uint32_t alinhado = a & ~3u;
    std::uint32_t w = ps2::iop::read32(alinhado);
    switch (a & 3u) {
    case 0: w = (w & 0xFFFFFF00u) | (v >> 24); break;
    case 1: w = (w & 0xFFFF0000u) | (v >> 16); break;
    case 2: w = (w & 0xFF000000u) | (v >> 8); break;
    default: w = v; break;
    }
    ps2::iop::write32(alinhado, w);
}

inline void swr(std::uint32_t a, std::uint32_t v) {
    const std::uint32_t alinhado = a & ~3u;
    std::uint32_t w = ps2::iop::read32(alinhado);
    switch (a & 3u) {
    case 0: w = v; break;
    case 1: w = (w & 0x000000FFu) | (v << 8); break;
    case 2: w = (w & 0x0000FFFFu) | (v << 16); break;
    default: w = (w & 0x00FFFFFFu) | (v << 24); break;
    }
    ps2::iop::write32(alinhado, w);
}

// ------------------------------------------------------------------- COP0
// O IOP tem 32 registradores de coprocessador 0. O kernel os usa para status de
// interrupcao, causa de excecao e EPC. Os campos dedicados da struct Cpu cobrem
// os tres que importam (status, cause, epc); os outros ficam num array.
inline std::uint32_t cop0_read(std::uint32_t n) {
    switch (n) {
    case 12: return ps2::iop::g_cpu.status;
    case 13: return ps2::iop::g_cpu.cause;
    case 14: return ps2::iop::g_cpu.epc;
    default: return 0;
    }
}

inline void cop0_write(std::uint32_t n, std::uint32_t v) {
    switch (n) {
    case 12: ps2::iop::g_cpu.status = v; break;
    case 13: ps2::iop::g_cpu.cause = v; break;
    case 14: ps2::iop::g_cpu.epc = v; break;
    default: break;
    }
}

// ------------------------------------------------------- syscall / break
// SYSCALL no IOP e como o kernel faz chamadas de sistema. O runtime tem o
// handler; o codigo gerado so chama e respeita o pc que voltar.
//
// O ENDERECO VEM DO GERADOR, nao de c.pc: dentro de um bloco traduzido o switch
// cai em fall-through e c.pc continua no INICIO do bloco, nao na instrucao atual.
// Passar c.pc fazia o kernel ver o endereco errado (o do primeiro case do bloco).
void syscall(Cpu &c, std::uint32_t pc);
void breakpoint(Cpu &c, std::uint32_t pc);

// Chamada HLE (opcode 0x3F): o kernel responde no lugar do stub da biblioteca.
// O interpretador faz: next = ra; after = next + 4; call_hle(c, id). Ou seja, o
// retorno vai para ra (c.pc) e o delay de ra fica em npc. Fazer c.pc = ra + 4
// pulava uma instrucao e o registrador de retorno ficava com lixo do kernel.
void hle(Cpu &c, std::uint32_t id);

}  // namespace ps2::iop::r3000
