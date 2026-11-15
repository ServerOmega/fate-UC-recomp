#include "r3000.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace recomp::r3000 {

namespace {

std::uint32_t u32(const std::uint8_t *p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::uint16_t u16(const std::uint8_t *p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

}  // namespace

// Le um IRX do IOP.
//
// O FORMATO NAO E O QUE PARECE.
// =============================
// Um .IRX comeca com \x7fELF e passa no teste de "e um ELF", mas o campo e_type
// NAO e 1 nem 2: e 0xFF80 (EXEC do IOP) ou 0xFF81 (versao 2 das relocacoes).
// A primeira versao deste parser recusou os 20 modulos com
// "tipo ELF 65408" (0xFF80) exatamente por isso.
//
// O codigo tambem nao esta numa secao .text: esta no primeiro segmento PT_LOAD
// (p_type == 1), e o tamanho vem de p_filesz/p_memsz dos PROGRAM HEADERS. As
// section headers existem para a tabela de relocacao (SHT_REL, tipo 9), que o
// loader usa ao carregar; para traduzir, o que importa e o segmento.
//
// A referencia e runtime/src/iop/kernel.cpp::load_file, que faz o mesmo caminho.
bool load_elf(const std::vector<std::uint8_t> &bytes, Program &out, std::string &erro) {
    if (bytes.size() < 52) {
        erro = "arquivo curto demais para um ELF";
        return false;
    }
    if (std::memcmp(bytes.data(), "\x7f" "ELF", 4) != 0) {
        erro = "nao comeca com \\x7fELF";
        return false;
    }
    if (bytes[4] != 1) {
        erro = "classe ELF " + std::to_string(bytes[4]) + " (esperado 1 = 32 bits)";
        return false;
    }
    if (bytes[5] != 1) {
        erro = "endianness " + std::to_string(bytes[5]) + " (esperado 1 = little endian)";
        return false;
    }
    const std::uint16_t machine = u16(&bytes[18]);
    if (machine != 8) {
        erro = "maquina " + std::to_string(machine) + " (esperado 8 = MIPS)";
        return false;
    }
    const std::uint16_t type = u16(&bytes[16]);
    // 0xFF80 e 0xFF81 sao os tipos do IOP; 1 e 2 aparecem em alguns modulos.
    if (type != 2 && type != 1 && type != 0xFF80 && type != 0xFF81) {
        erro = "tipo ELF " + std::to_string(type) + " (esperado 0xFF80/0xFF81 do IOP)";
        return false;
    }

    const std::uint32_t entry = u32(&bytes[24]);
    const std::uint32_t phoff = u32(&bytes[28]);
    const std::uint16_t phentsize = u16(&bytes[42]);
    const std::uint16_t phnum = u16(&bytes[44]);

    if (phentsize < 32 || phnum == 0) {
        erro = "sem program headers";
        return false;
    }
    if (std::uint64_t{phoff} + std::uint64_t{phnum} * phentsize > bytes.size()) {
        erro = "program headers fora do arquivo";
        return false;
    }

    // Primeiro segmento PT_LOAD: e onde o codigo esta.
    std::uint32_t load_off = 0, load_size = 0, load_vaddr = 0, mem_size = 0;
    bool achou_load = false;
    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::size_t ph = phoff + std::size_t{i} * phentsize;
        const std::uint32_t ptype = u32(&bytes[ph]);
        if (ptype == 1 && !achou_load) {
            load_off = u32(&bytes[ph + 4]);
            load_vaddr = u32(&bytes[ph + 8]);
            load_size = u32(&bytes[ph + 16]);
            mem_size = u32(&bytes[ph + 20]);
            achou_load = true;
        }
    }
    if (!achou_load || load_size == 0) {
        erro = "sem segmento PT_LOAD com conteudo";
        return false;
    }
    if (std::uint64_t{load_off} + load_size > bytes.size()) {
        erro = "o segmento PT_LOAD passa do fim do arquivo";
        return false;
    }

    out.base = load_vaddr;
    out.end = load_vaddr + std::max(load_size, mem_size);
    out.entry = entry;

    // O codigo comeca no inicio do segmento. O entry point e um offset dentro
    // dele (o loader soma a base de memoria), entao traduzimos o segmento
    // inteiro a partir de load_vaddr.
    for (std::uint32_t o = 0; o + 4 <= load_size; o += 4) {
        Insn in;
        in.addr = load_vaddr + o;
        in.word = u32(&bytes[load_off + o]);
        in.op = static_cast<std::uint8_t>((in.word >> 26) & 0x3F);
        in.rs = static_cast<std::uint8_t>((in.word >> 21) & 0x1F);
        in.rt = static_cast<std::uint8_t>((in.word >> 16) & 0x1F);
        in.rd = static_cast<std::uint8_t>((in.word >> 11) & 0x1F);
        in.sa = static_cast<std::uint8_t>((in.word >> 6) & 0x1F);
        in.funct = static_cast<std::uint8_t>(in.word & 0x3F);
        in.imm = static_cast<std::uint16_t>(in.word & 0xFFFF);
        in.simm = static_cast<std::int16_t>(in.imm);
        in.target = (in.word & 0x03FFFFFFu) << 2;
        in.branch = static_cast<std::int32_t>(in.simm) * 4;
        out.text.push_back(in);
    }

    if (out.text.empty()) {
        erro = "segmento PT_LOAD sem instrucoes";
        return false;
    }
    return true;
}

void find_functions(Program &p) {
    // Entradas: o entry point e todo destino de JAL. Sem isso o gerador entra no
    // meio de uma funcao e perde o que veio antes (ajuste de pilha, carregamento
    // de gp), gerando codigo que roda uma vez e quebra na segunda.
    std::map<std::uint32_t, bool> entradas;
    entradas[p.entry] = true;
    for (const Insn &in : p.text) {
        if (in.op == 3) {  // JAL
            const std::uint32_t destino = (in.addr & 0xF0000000u) | in.target;
            entradas[destino] = true;
        }
    }
    p.func_bases.clear();
    for (const auto &[addr, _] : entradas) p.func_bases.push_back(addr);
}

}  // namespace recomp::r3000
