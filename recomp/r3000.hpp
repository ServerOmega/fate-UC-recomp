#pragma once

// Tradutor R3000 (IOP) — a CPU auxiliar do PS2.
//
// POR QUE ESTE ARQUIVO EXISTE
// ===========================
// O PS2 tem duas CPUs: a EE (R5900, 128 bits, com 2 VU) e o IOP (R3000A,
// MIPS32, 33 MHz). O projeto original traduz a EE e as VU para C++, e mantem o
// IOP num interpretador (runtime/src/iop/cpu.cpp). Isso significa que os 21
// modulos .IRX do disco (128.628 instrucoes) sao executados instrucao por
// instrucao, nao compilados.
//
// Este tradutor fecha essa lacuna para que o IOP tambem seja nativo.
//
// O QUE O R3000 TEM QUE O R5900 NAO TEM (e vice-versa)
// ===================================================
// Igual: ADDIU, LW, SW, JAL, LUI, BNE, BEQ, J, ANDI, LBU, SB, LHU, SH, ORI,
//        LH, SLTI, LB, BLEZ, SLTIU, BGTZ, SPECIAL (ADDU, SLL, JR, SRA, SLT...)
// Diferente:
//   - R3000 NAO tem 64 bits: nada de DADD, DSUB, DSLL, LD, SD, LQ, SQ.
//   - R3000 NAO tem MMI/COP1/COP2.
//   - R3000 tem load-delay slot: a instrucao seguinte a um load NAO ve o valor
//     carregado. O R5900 nao tem isso. Isto e uma diferenca SEMANTICA, nao
//     apenas de instruction set, e o tradutor precisa respeitar.
//   - R3000 tem COP0 com registradores diferentes (Status 12, Cause 13, EPC 14,
//     BadVaddr 8), e as excecoes entram em 0x80000080.
//   - Branch delay slot: igual ao R5900, e o tradutor ja trata.
//
// MEDIDO: das 128.628 instrucoes nos 20 modulos, os opcodes usados sao 34 e
// 15 deles cobrem 95%. Zero COP2. 189 COP0 (syscalls do kernel do IOP).
// Ver analisar_iop.py na raiz do projeto.

#include <cstdint>
#include <string>
#include <vector>

namespace recomp::r3000 {

// Uma instrucao decodificada.
struct Insn {
    std::uint32_t addr = 0;
    std::uint32_t word = 0;
    // Decodificacao ja feita, para o gerador nao repetir a aritmetica de bits.
    std::uint8_t op = 0;    // opcode primario (bits 31..26)
    std::uint8_t rs = 0;
    std::uint8_t rt = 0;
    std::uint8_t rd = 0;
    std::uint8_t sa = 0;
    std::uint8_t funct = 0; // para op == 0 (SPECIAL)
    std::uint16_t imm = 0;
    std::uint32_t target = 0; // para J/JAL (26 bits << 2)
    std::int32_t simm = 0;    // imm com sinal
    std::int32_t branch = 0;  // deslocamento de branch em bytes, com sinal
};

// Uma funcao traduzida: comeca num endereco e vai ate um retorno.
struct Function {
    std::uint32_t base = 0;
    std::uint32_t end = 0;  // exclusivo
    std::vector<Insn> insns;
};

// O programa inteiro de um modulo.
struct Program {
    std::uint32_t base = 0;
    std::uint32_t end = 0;      // fim exclusivo do codigo
    std::uint32_t entry = 0;
    std::string name;
    std::vector<Insn> text;
    // Endereços que sao entrada de funcao (entry point + destino de JAL).
    // O gerador precisa disso: entrar no meio de uma funcao gera codigo errado.
    std::vector<std::uint32_t> func_bases;
};

// Le um ELF MIPS32 (formato .IRX do IOP) e devolve as instrucoes da .text.
// Devolve falso e preenche `erro` quando o arquivo nao e um ELF MIPS32 valido.
bool load_elf(const std::vector<std::uint8_t> &bytes, Program &out, std::string &erro);

// Analisa o fluxo: acha entradas de funcao (destinos de JAL e ponteiros em
// tabelas), para o gerador saber onde comecar uma funcao nova em vez de cair no
// meio de outra.
void find_functions(Program &p);

}  // namespace recomp::r3000
