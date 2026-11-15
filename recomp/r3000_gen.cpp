#include "r3000_gen.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace recomp::r3000 {

namespace {

// Sem sufixo: quem usa decide se precisa de "u". Antes isto devolvia
// "0x...u" e o gerador concatenava outro, produzindo "0x...uu" (erro de compilacao).
std::string hex(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// Nome do registrador como expressao C++.
std::string r(std::uint32_t n) {
    if (n == 0) return "0u";
    char b[16];
    std::snprintf(b, sizeof b, "c.r[%u]", n);
    return b;
}

// Offset de memoria, com sinal, no formato "(base + N)".
std::string mem(const std::string &base, std::int32_t off) {
    if (off == 0) return base;
    char b[32];
    if (off > 0) {
        std::snprintf(b, sizeof b, "(%s + %d)", base.c_str(), off);
    } else {
        std::snprintf(b, sizeof b, "(%s - %d)", base.c_str(), -off);
    }
    return b;
}

// ============================================================================
// Alcancabilidade: separa codigo de dados.
// ============================================================================

// Anda o grafo de fluxo a partir de `bases` e devolve o conjunto de enderecos
// que sao instrucao de verdade. Tudo o que nao for alcancavel e dado.
std::set<std::uint32_t> alcancaveis(const Program &p) {
    // Mapa endereco -> indice, para achar a instrucao pelo endereco.
    std::map<std::uint32_t, std::size_t> por_addr;
    for (std::size_t i = 0; i < p.text.size(); ++i) por_addr[p.text[i].addr] = i;

    std::set<std::uint32_t> visto;
    std::deque<std::uint32_t> fila;

    // Sementes: entry point e todo destino de JAL (que ja sao entradas de funcao).
    fila.push_back(p.entry);
    for (std::uint32_t f : p.func_bases) fila.push_back(f);

    while (!fila.empty()) {
        std::uint32_t pc = fila.front();
        fila.pop_front();

        // Anda linearmente ate um ponto de parada (branch, jump, retorno).
        for (;;) {
            auto it = por_addr.find(pc);
            if (it == por_addr.end()) break;      // caiu em dado: para
            if (visto.count(pc)) break;           // ja traduzido
            visto.insert(pc);

            const Insn &in = p.text[it->second];
            const std::uint32_t prox = pc + 4;

            if (in.op == 0 && in.funct == 8) {          // JR
                // O DELAY SLOT DO JR E CODIGO E E ALCANCAVEL.
                // Ele executa sempre, antes do salto. Nao marcar aqui deixava
                // 0x7150 (nop depois de JR $ra) fora do conjunto e a ponte
                // escapava nele - 840 escapes por execucao.
                visto.insert(prox);
                if (in.rs == 31) break;                 // JR RA = retorno
                // JR de registrador qualquer: nao da para seguir sem saber o
                // valor; para aqui. O destino sera coberto se for JAL em outro
                // lugar ou se estiver em func_bases.
                break;
            }
            if (in.op == 3) {                            // JAL: segue e empilha destino
                const std::uint32_t d = (pc & 0xF0000000u) | in.target;
                fila.push_back(d);
                // JAL tem delay slot: a proxima instrucao executa antes do salto
                pc = prox;
                continue;
            }
            if (in.op == 2) {                            // J
                const std::uint32_t d = (pc & 0xF0000000u) | in.target;
                fila.push_back(d);
                break;
            }
            if (in.op == 4 || in.op == 5) {              // BEQ, BNE
                const std::uint32_t alvo = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(pc) + 4 + in.branch);
                fila.push_back(alvo);
                // O delay slot executa de qualquer forma; o fall-through so
                // continua se o branch for condicional (BEQ/BNE sao).
                pc = prox;
                continue;
            }
            if (in.op == 1) {                            // REGIMM (BLTZ, BGEZ, ...)
                const std::uint32_t alvo = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(pc) + 4 + in.branch);
                fila.push_back(alvo);
                pc = prox;
                continue;
            }
            if (in.op == 6 || in.op == 7) {              // BLEZ, BGTZ
                const std::uint32_t alvo = static_cast<std::uint32_t>(
                    static_cast<std::int64_t>(pc) + 4 + in.branch);
                fila.push_back(alvo);
                pc = prox;
                continue;
            }
            pc = prox;
        }
    }
    return visto;
}

// ============================================================================
// Emissao de uma instrucao.
// ============================================================================

// Devolve a linha de C++ que implementa `in`. Vazio quando a instrucao nao tem
// efeito visivel (NOP).
std::string emitir(const Insn &in, const std::string &ns, const std::string &delay_slot_) {
    std::ostringstream o;
    const std::uint32_t rs = in.rs, rt = in.rt, rd = in.rd;

    switch (in.op) {
    case 0:  // SPECIAL
        switch (in.funct) {
        case 0:  // SLL (NOP quando tudo zero)
            if (in.word == 0) return "";
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rt) << " << " << int(in.sa) << "u;";
            return o.str();
        case 2:  // SRL
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rt) << " >> " << int(in.sa) << "u;";
            return o.str();
        case 3:  // SRA (aritmetico)
            if (rd == 0) return "";
            o << r(rd) << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
              << r(rt) << ") >> " << int(in.sa) << ");";
            return o.str();
        case 4:  // SLLV
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rt) << " << (" << r(rs) << " & 31u);";
            return o.str();
        case 6:  // SRLV
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rt) << " >> (" << r(rs) << " & 31u);";
            return o.str();
        case 7:  // SRAV
            if (rd == 0) return "";
            o << r(rd) << " = static_cast<std::uint32_t>(static_cast<std::int32_t>("
              << r(rt) << ") >> (" << r(rs) << " & 31u));";
            return o.str();
        case 8:  // JR — salto para rs, com o delay slot executado antes
            if (rs == 31) o << delay_slot_ << " return;  // JR RA";
            else o << delay_slot_ << " c.pc = " << r(rs) << "; return;  // JR";
            return o.str();
        case 9:  // JALR
            // rd pode ser 0 (JALR r0, rs): escrever em r0 gera "0u = ..." que nao
            // compila. r0 e sempre zero por definicao, entao so emitir o store
            // quando rd != 0.
            if (rd != 0) o << r(rd) << " = " << hex(in.addr + 8) << "u; ";
            o << delay_slot_ << " c.pc = " << r(rs) << "; return;  // JALR";
            return o.str();
        case 10:  // MOVZ
            if (rd == 0) return "";
            o << "if (" << r(rt) << " == 0) " << r(rd) << " = " << r(rs) << ";";
            return o.str();
        case 11:  // MOVN
            if (rd == 0) return "";
            o << "if (" << r(rt) << " != 0) " << r(rd) << " = " << r(rs) << ";";
            return o.str();
        case 12:  // SYSCALL
            o << "syscall(c, " << hex(in.addr) << "u); if (c.pc != " << hex(in.addr + 4)
              << "u) return;  // SYSCALL";
            return o.str();
        case 13:  // BREAK
            o << "breakpoint(c, " << hex(in.addr) << "u); return;  // BREAK";
            return o.str();
        case 16:  // MFHI
            if (rd == 0) return "";
            o << r(rd) << " = c.hi;";
            return o.str();
        case 17:  // MTHI
            o << "c.hi = " << r(rs) << ";";
            return o.str();
        case 18:  // MFLO
            if (rd == 0) return "";
            o << r(rd) << " = c.lo;";
            return o.str();
        case 19:  // MTLO
            o << "c.lo = " << r(rs) << ";";
            return o.str();
        case 24:  // MULT (com sinal, resultado em hi:lo)
            o << "{ std::int64_t p = std::int64_t(static_cast<std::int32_t>(" << r(rs)
              << ")) * std::int64_t(static_cast<std::int32_t>(" << r(rt)
              << ")); c.lo = std::uint32_t(p); c.hi = std::uint32_t(std::uint64_t(p) >> 32); }";
            return o.str();
        case 25:  // MULTU
            o << "{ std::uint64_t p = std::uint64_t(" << r(rs) << ") * std::uint64_t("
              << r(rt) << "); c.lo = std::uint32_t(p); c.hi = std::uint32_t(p >> 32); }";
            return o.str();
        case 26:  // DIV (com sinal)
            o << "{ std::int32_t a = std::int32_t(" << r(rs) << "), b = std::int32_t("
              << r(rt) << "); if (b != 0) { if (a == INT32_MIN && b == -1) { c.lo = std::uint32_t(a); c.hi = 0u; }"
              << " else { c.lo = std::uint32_t(a / b); c.hi = std::uint32_t(a % b); } } }";
            return o.str();
        case 27:  // DIVU
            o << "{ std::uint32_t a = " << r(rs) << ", b = " << r(rt)
              << "; if (b != 0) { c.lo = a / b; c.hi = a % b; } }";
            return o.str();
        case 32:  // ADD (com overflow)
            if (rd == 0) return "";
            o << r(rd) << " = std::uint32_t(std::int32_t("
              << r(rs) << ") + std::int32_t(" << r(rt) << "));  // sem trap: o IOP nao usa";
            return o.str();
        case 33:  // ADDU
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " + " << r(rt) << ";";
            return o.str();
        case 34:  // SUB
        case 35:  // SUBU
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " - " << r(rt) << ";";
            return o.str();
        case 36:  // AND
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " & " << r(rt) << ";";
            return o.str();
        case 37:  // OR
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " | " << r(rt) << ";";
            return o.str();
        case 38:  // XOR
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " ^ " << r(rt) << ";";
            return o.str();
        case 39:  // NOR
            if (rd == 0) return "";
            o << r(rd) << " = ~(" << r(rs) << " | " << r(rt) << ");";
            return o.str();
        case 42:  // SLT
            if (rd == 0) return "";
            o << r(rd) << " = std::int32_t(" << r(rs) << ") < std::int32_t(" << r(rt) << ") ? 1u : 0u;";
            return o.str();
        case 43:  // SLTU
            if (rd == 0) return "";
            o << r(rd) << " = " << r(rs) << " < " << r(rt) << " ? 1u : 0u;";
            return o.str();
        default:
            o << "// SPECIAL funct " << int(in.funct) << " nao traduzido (PC "
              << hex(in.addr) << ")";
            return o.str();
        }

    case 1: {  // REGIMM
        const std::uint32_t alvo = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(in.addr) + 4 + in.branch);
        const std::string cond = [&]() -> std::string {
            switch (rt) {
            case 0: case 16: return "std::int32_t(" + r(rs) + ") < 0";
            case 1: case 17: return "std::int32_t(" + r(rs) + ") >= 0";
            default: return "";   // REGIMM desconhecido: nao emitir, nao inventar condicao
            }
        }();
        // Sem condicao (rt nao reconhecido) a instrucao nao tem traducao: devolver
        // linha vazia. Devolver "false" gerava "if (false) { }" e, com o delay slot
        // vazio, o comentario engolia o return e deixava a chave aberta - o C++
        // quebrava com "function definition is not allowed here".
        if (cond.empty()) return "";
        // BLTZAL/BGEZAL gravam o retorno em $31.
        const std::string link = (rt == 16 || rt == 17)
            ? r(31) + " = " + hex(in.addr + 8) + "u; "
            : "";
        o << link << "if (" << cond << ") {";
        o << " " << delay_slot_ << " c.pc = " << hex(alvo) << "u; return; }";
        return o.str();
    }

    case 2:  // J
        o << delay_slot_ << " c.pc = " << hex((in.addr & 0xF0000000u) | in.target)
          << "u; return;  // J";
        return o.str();

    case 3:  // JAL
        o << r(31) << " = " << hex(in.addr + 8) << "u; " << delay_slot_
          << " c.pc = " << hex((in.addr & 0xF0000000u) | in.target) << "u; return;";
        return o.str();

    case 4:   // BEQ
    case 5: { // BNE
        const std::uint32_t alvo = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(in.addr) + 4 + in.branch);
        const char *cmp = in.op == 4 ? "==" : "!=";
        // O delay slot executa antes do salto: emiti-lo aqui dentro do bloco.
        o << "if (" << r(rs) << " " << cmp << " " << r(rt) << ") {";
        o << " " << delay_slot_ << " c.pc = " << hex(alvo) << "u; return; }";
        return o.str();
    }

    case 6:   // BLEZ
    case 7: { // BGTZ
        const std::uint32_t alvo = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(in.addr) + 4 + in.branch);
        const char *cmp = in.op == 6 ? "<=" : ">";
        o << "if (std::int32_t(" << r(rs) << ") " << cmp << " 0) {";
        o << " " << delay_slot_ << " c.pc = " << hex(alvo) << "u; return; }";
        return o.str();
    }

    case 8:  // ADDI
    case 9:  // ADDIU
        if (rt == 0) return "";
        o << r(rt) << " = " << r(rs) << " + " << hex(static_cast<std::uint32_t>(in.simm)) << ";";
        return o.str();

    case 10:  // SLTI
        if (rt == 0) return "";
        o << r(rt) << " = std::int32_t(" << r(rs) << ") < " << in.simm << " ? 1u : 0u;";
        return o.str();

    case 11:  // SLTIU
        if (rt == 0) return "";
        o << r(rt) << " = " << r(rs) << " < " << hex(static_cast<std::uint32_t>(in.simm))
          << " ? 1u : 0u;";
        return o.str();

    case 12:  // ANDI
        if (rt == 0) return "";
        o << r(rt) << " = " << r(rs) << " & " << hex(in.imm) << ";";
        return o.str();

    case 13:  // ORI
        if (rt == 0) return "";
        o << r(rt) << " = " << r(rs) << " | " << hex(in.imm) << ";";
        return o.str();

    case 14:  // XORI
        if (rt == 0) return "";
        o << r(rt) << " = " << r(rs) << " ^ " << hex(in.imm) << ";";
        return o.str();

    case 15:  // LUI
        if (rt == 0) return "";
        o << r(rt) << " = " << hex(static_cast<std::uint32_t>(in.imm) << 16) << ";";
        return o.str();

    case 16: {  // COP0
        if (rs == 0) {  // MFC0
            o << r(rt) << " = cop0_read(" << int(rd) << ");";
        } else if (rs == 4) {  // MTC0
            o << "cop0_write(" << int(rd) << ", " << r(rt) << ");";
        } else {
            o << "// COP0 rs=" << int(rs) << " (PC " << hex(in.addr) << ")";
        }
        return o.str();
    }

    case 32:  // LB
        if (rt == 0) return "";
        o << r(rt) << " = std::uint32_t(std::int32_t(ld8(" << mem(r(rs), in.simm) << ")));";
        return o.str();

    case 33:  // LH
        if (rt == 0) return "";
        o << r(rt) << " = std::uint32_t(std::int32_t(ld16(" << mem(r(rs), in.simm) << ")));";
        return o.str();

    case 35:  // LW
        if (rt == 0) return "";
        o << r(rt) << " = ld32(" << mem(r(rs), in.simm) << ");";
        return o.str();

    case 36:  // LBU
        if (rt == 0) return "";
        o << r(rt) << " = ld8(" << mem(r(rs), in.simm) << ");";
        return o.str();

    case 37:  // LHU
        if (rt == 0) return "";
        o << r(rt) << " = ld16(" << mem(r(rs), in.simm) << ");";
        return o.str();

    case 40:  // SB
        o << "st8(" << mem(r(rs), in.simm) << ", std::uint8_t(" << r(rt) << "));";
        return o.str();

    case 41:  // SH
        o << "st16(" << mem(r(rs), in.simm) << ", std::uint16_t(" << r(rt) << "));";
        return o.str();

    case 43:  // SW
        o << "st32(" << mem(r(rs), in.simm) << ", " << r(rt) << ");";
        return o.str();

    case 34:    // LWL
    case 38:    // LWR
        // LWL/LWR carregam bytes parciais. Sem eles um boot de PS1 nao funciona,
        // mas os modulos do IOP quase nao usam (80 de 147.752). Implementados
        // como leitura de palavra para nao gerar codigo errado silencioso.
        if (rt == 0) return "";
        o << r(rt) << " = lwl(" << mem(r(rs), in.simm) << ", " << r(rt) << ");  // APROXIMADO";
        return o.str();

    case 42:    // SWL
    case 46:    // SWR
        o << "swl(" << mem(r(rs), in.simm) << ", " << r(rt) << ");  // APROXIMADO";
        return o.str();

    case 48:    // LWC0
        if (rt == 0) return "";
        o << r(rt) << " = ld32(" << mem(r(rs), in.simm) << ");  // LWC0 (cache isolado: vira load)";
        return o.str();

    case 56:    // SWC0
        o << "st32(" << mem(r(rs), in.simm) << ", " << r(rt) << ");  // SWC0";
        return o.str();

    case 50:    // LWC2 (sem GTE no PS2: os modulos nao usam)
        o << "// LWC2 nao existe no PS2 (PC " << hex(in.addr) << ")";
        return o.str();

    case 58:    // SWC2
        o << "// SWC2 nao existe no PS2 (PC " << hex(in.addr) << ")";
        return o.str();

    case 63: {  // HLE (0x3F): chamada ao kernel no lugar de um stub de biblioteca
        // Sem este case a instrucao virava comentario e a chamada nunca acontecia:
        // as threads nunca dormiam e o IOP girava a 614 mil ciclos por quadro
        // (contra 26 mil do normal), com o chip de som zerado.
        o << "hle(c, " << hex(in.addr) << "u); return;  // HLE";
        return o.str();
    }

    default:
        o << "// opcode " << int(in.op) << " nao traduzido (PC " << hex(in.addr) << "); c.pc = kUnwind; return;";
        return o.str();
    }
}

}  // namespace

// ============================================================================
// Geracao do arquivo
// ============================================================================

std::string generate_cpp(const Program &p, const std::string &ns) {
    // CODIGO ALCANCADO POR PONTEIRO
    // =============================
    // A analise a partir de entry/JAL cobre 62,6% das palavras. O resto que e
    // codigo de verdade so aparece em runtime: o kernel recebe o endereco de uma
    // funcao (callback de thread, tabela de syscall, vetor de interrupcao) e
    // salta para la sem nenhum J/JAL apontando para ela. Medido no MCSERV: o pc
    // 0x280 e um prologo classico (addiu sp,sp,-40) com nenhum salto direto.
    //
    // Tratamento: alem do alcancavel, procurar os enderecos que PARECEM codigo -
    // palavra cujo opcode e valido no R3000 - e somar ao conjunto. Custa codigo
    // gerado a mais; ganha o que o jogo realmente executa.
    //
    // PALAVRA ZERO E CODIGO VALIDO: e `nop`, e aparece como delay slot de JR/J
    // (MCSERV/TSNDDRVC tem `JR $ra` seguido de `00000000`). Descartar w == 0
    // deixava o delay slot fora da tabela e a ponte escapava nele - eram 840
    // escapes por execucao.
    std::set<std::uint32_t> codigo = alcancaveis(p);
    const std::size_t so_alcancavel = codigo.size();
    for (const Insn &in : p.text) {
        if (codigo.count(in.addr)) continue;
        if (in.op > 63) continue;
        // 0x2F..0x3E e faixa nao usada no R3000: se aparecer, e dado.
        if (in.op >= 0x2F && in.op <= 0x3E) continue;
        codigo.insert(in.addr);
    }
    // ZEROS QUE SAO DELAY SLOT, e so esses. Incluir todo w == 0 encheria o codigo
    // gerado com o .bss inteiro; o nop que importa e o que segue um salto, porque
    // ele JA esta em da_funcao pela regra do delay slot. Aqui so se garante que a
    // analise o reconheca como codigo (o filtro acima ja aceita op == 0, entao o
    // nop entra naturalmente) - e o que faltava era o JR/J incluir o proximo
    // endereco no bloco. Nada a fazer aqui alem de documentar.
    std::fprintf(stderr, "  %s: %zu alcancaveis + %zu por ponteiro = %zu de %zu palavras\n",
                 p.name.c_str(), so_alcancavel, codigo.size() - so_alcancavel,
                 codigo.size(), p.text.size());

    std::map<std::uint32_t, std::size_t> por_addr;
    for (std::size_t i = 0; i < p.text.size(); ++i) por_addr[p.text[i].addr] = i;

    // Descobre os pontos de parada: onde uma funcao termina. Um bloco de codigo
    // comeca em func_base e vai ate um branch/jump/return, e os alvos de branch
    // viram labels dentro da mesma funcao.
    std::set<std::uint32_t> bases(p.func_bases.begin(), p.func_bases.end());
    bases.insert(p.entry);
    // Bases novas: cada trecho de codigo que NINGUEM alcanca vira a sua propria
    // funcao. Sem isto o endereco entra em `codigo` mas nao e emitido em nenhum
    // switch, e o despacho continua sem entrada (era o caso do MCSERV 0x280).
    //
    // CUIDADO: criar base para todo endereco orfao gerava funcoes de 1 instrucao
    // emendadas em cima de blocos que ja terminam em return, e o C++ sai com
    // "function definition is not allowed here". So criar base quando o endereco
    // anterior NAO e codigo (buraco de verdade) ou quando o anterior termina em
    // branch/jump - nunca no meio de um bloco.
    {
        std::set<std::uint32_t> cobertas(bases.begin(), bases.end());
        for (std::uint32_t a : codigo) {
            if (cobertas.count(a)) continue;
            auto ant = por_addr.find(a - 4);
            if (ant != por_addr.end() && codigo.count(a - 4)) {
                const Insn &p4 = p.text[ant->second];
                const bool termina = (p4.op == 1 || p4.op == 2 || p4.op == 3 ||
                                      p4.op == 4 || p4.op == 5 || p4.op == 6 || p4.op == 7 ||
                                      (p4.op == 0 && (p4.funct == 8 || p4.funct == 9 ||
                                                      p4.funct == 12 || p4.funct == 13)));
                if (!termina) continue;   // meio de bloco: deixa o dono cuidar
            }
            bases.insert(a);
            cobertas.insert(a);
        }
    }

    std::ostringstream o;
    o << "// GERADO por r3000_gen — nao editar.\n";
    o << "// Modulo: " << p.name << "\n";
    o << "// " << codigo.size() << " instrucoes alcancaveis de " << p.text.size()
      << " palavras no segmento.\n";
    o << "#include \"ps2/iop/r3000_abi.hpp\"\n";
    o << "#include \"ps2/iop/r3000_native.hpp\"\n\n";
    o << "namespace ps2::iop::gen::" << ns << " {\n\n";
    o << "using namespace ps2::iop::r3000;\n\n";

    // Emite uma funcao por base alcancavel.
    //
    // Cada funcao cobre TODOS os blocos alcancaveis a partir dela, nao apenas o
    // caminho linear. Um bloco vai da entrada ate um branch/jump/return; os
    // destinos de branch viram cases dentro da mesma funcao, e o codigo gerado
    // ja faz "c.pc = alvo; return;" para o dispatcher reentrar.
    //
    // MAPA DE ENTRADAS COMPLETO
    // =========================
    // A tabela registrada tem que cobrir TODO endereco que a funcao despacha no
    // switch, nao so o inicio. Medido: registrando so os inicios, o despacho
    // nativo ficava em 2,0% e o resto escapava para o interpretador - um salto
    // para o meio da funcao (destino de branch, retorno por jr $ra) nao achava
    // entrada. O switch ja tem estes cases; basta dizer quais sao a ponte.
    std::map<std::uint32_t, std::vector<std::uint32_t>> enderecos_da_funcao;
    std::map<std::uint32_t, std::map<std::uint32_t, std::uint16_t>> ciclos_da_funcao;

    for (std::uint32_t base : bases) {
        auto it = por_addr.find(base);
        if (it == por_addr.end()) continue;
        if (!codigo.count(base)) continue;

        // Blocos desta funcao: andar a partir do base e seguir os branches,
        // parando quando o endereco ja pertence a OUTRA funcao (que tem o seu).
        std::set<std::uint32_t> da_funcao;
        std::deque<std::uint32_t> fila;
        fila.push_back(base);
        while (!fila.empty()) {
            std::uint32_t pc = fila.front();
            fila.pop_front();
            while (por_addr.count(pc) && codigo.count(pc) && !da_funcao.count(pc)) {
                // Parar se este endereco e base de outra funcao.
                bool de_outra = false;
                for (std::uint32_t b : bases) {
                    if (b != base && b == pc) { de_outra = true; break; }
                }
                if (de_outra) break;

                da_funcao.insert(pc);
                const Insn &in = p.text[por_addr[pc]];
                const std::uint32_t prox = pc + 4;

                // Destinos de branch/REGIMM viram continuação da fila.
                if (in.op == 1 || in.op == 4 || in.op == 5 || in.op == 6 || in.op == 7) {
                    const std::uint32_t alvo = static_cast<std::uint32_t>(
                        static_cast<std::int64_t>(pc) + 4 + in.branch);
                    if (codigo.count(alvo)) fila.push_back(alvo);
                    pc = prox;
                    continue;
                }
                if (in.op == 2) {  // J
                    const std::uint32_t d = (pc & 0xF0000000u) | in.target;
                    if (codigo.count(d)) fila.push_back(d);
                    // delay slot do J: executa antes do salto, fica neste bloco
                    if (por_addr.count(prox) && codigo.count(prox)) da_funcao.insert(prox);
                    break;
                }
                if (in.op == 3) {  // JAL: continua no delay slot
                    pc = prox;
                    continue;
                }
                if (in.op == 0 && in.funct == 8) {   // JR
                    // O delay slot do JR faz parte deste bloco: ele executa
                    // antes do salto. Sem incluir aqui, o gerador nao emite e a
                    // ponte despacha para um endereco sem entrada (os 840
                    // escapes restantes vinham disto).
                    if (por_addr.count(prox) && codigo.count(prox)) da_funcao.insert(prox);
                    break;
                }
                if (in.op == 0 && (in.funct == 12 || in.funct == 13)) break;  // SYSCALL/BREAK
                pc = prox;
            }
        }

        if (da_funcao.empty()) continue;

        // Guardar os enderecos desta funcao para a tabela de entradas.
        enderecos_da_funcao[base].assign(da_funcao.begin(), da_funcao.end());

        // Contagem estatica de instrucoes por ponto de entrada: do pc ate a
        // primeira instrucao que devolve o controle, mais o delay slot dela.
        // O switch cai em fall-through em ordem de endereco, entao o caminho
        // linear e pc, pc+4, pc+8... e para no primeiro return.
        {
            std::map<std::uint32_t, std::uint16_t> ciclos;
            for (std::uint32_t pc0 : da_funcao) {
                std::uint32_t pc = pc0;
                std::uint16_t n = 0;
                for (;;) {
                    auto it2 = por_addr.find(pc);
                    if (it2 == por_addr.end() || !da_funcao.count(pc)) break;
                    const Insn &in = p.text[it2->second];
                    ++n;   // a instrucao em si
                    const bool termina = (in.op == 1 || in.op == 2 || in.op == 3 ||
                                          in.op == 4 || in.op == 5 || in.op == 6 || in.op == 7 ||
                                          (in.op == 0 && (in.funct == 8 || in.funct == 9 ||
                                                          in.funct == 12 || in.funct == 13)));
                    if (termina) {
                        // O delay slot executa antes do salto.
                        if (da_funcao.count(pc + 4)) ++n;
                        break;
                    }
                    pc += 4;
                }
                ciclos[pc0] = n ? n : 1;
            }
            ciclos_da_funcao[base] = std::move(ciclos);
        }

        o << "void f_" << std::hex << base << std::dec << "(Cpu &c, std::uint32_t depth) {\n";
        o << "    (void)depth;\n";
        o << "    if (c.pc == 0) c.pc = " << hex(base) << "u;\n";
        o << "    switch (c.pc) {\n";

        // Emite em ordem de endereco. O switch faz fall-through, entao o caminho
        // linear sai de graca; o branch tomado faz return e o dispatcher volta.
        for (std::uint32_t pc : da_funcao) {
            o << "    case " << hex(pc) << "u:\n";
            const Insn &in = p.text[por_addr[pc]];
            // O delay slot e a instrucao seguinte, quando esta e um branch.
            // No MIPS ela executa SEMPRE, antes do salto: emitir aqui dentro.
            //
            // JR/JALR/J TAMBEM TEM DELAY SLOT. Deixa-los de fora fazia o JR $ra
            // fazer "return" e a ponte despachar o endereco do delay slot, que
            // nao tem entrada na tabela (pertence ao bloco seguinte) - era a
            // origem dos 840 escapes restantes. Medido no MCSERV: 0x38 = JR $ra,
            // 0x3C = nop (delay), 0x40 = proxima funcao.
            std::string delay;
            const bool eh_branch = (in.op == 1 || in.op == 2 || in.op == 3 ||
                                    in.op == 4 || in.op == 5 ||
                                    in.op == 6 || in.op == 7 ||
                                    (in.op == 0 && (in.funct == 8 || in.funct == 9)));
            if (eh_branch) {
                auto depois = por_addr.find(pc + 4);
                if (depois != por_addr.end() && codigo.count(pc + 4)) {
                    std::string d = emitir(p.text[depois->second], ns, "");
                    // DELAY SLOT QUE E SALTO MATA O BRANCH
                    // ===================================
                    // No MIPS o delay slot executa SEMPRE. Se ele proprio e um
                    // salto (JR/JALR/J), o desvio do branch nunca acontece: quem
                    // manda e o delay slot. Emitir os dois gera codigo inalcancavel
                    // e, pior, o comentario "// JR" do delay engole o
                    // "c.pc = alvo; return; }" do branch, quebrando o C++.
                    const Insn &ds = p.text[depois->second];
                    const bool ds_salta = (ds.op == 2 || ds.op == 3 ||
                                           (ds.op == 0 && (ds.funct == 8 || ds.funct == 9)));
                    if (ds_salta) {
                        // so o delay slot, com o comentario numa linha propria
                        std::string codigo_d = d, comentario;
                        const auto pos = d.find("//");
                        if (pos != std::string::npos) {
                            comentario = d.substr(pos);
                            codigo_d = d.substr(0, pos);
                        }
                        o << "        " << codigo_d << "\n";
                        if (!comentario.empty()) o << "        " << comentario << "\n";
                        o << "        return;   // delay slot e salto: branch nao toma efeito\n";
                        continue;
                    }
                    if (d.rfind("//", 0) == 0) {
                        o << "    // delay slot sem traducao: " << d.substr(2) << "\n";
                        d.clear();
                    }
                    delay = d;
                }
            }
            const std::string linha = emitir(in, ns, delay);
            if (!linha.empty()) o << "        " << linha << "\n";
        }

        o << "    default: c.pc = kUnwind; return;\n";
        o << "    }\n";
        o << "}\n\n";
    }

    // Tabela de entradas: um par por ENDERECO que alguma funcao despacha, nao
    // so por inicio de funcao. Assim um salto para o meio de um bloco (destino
    // de branch, retorno por jr) ainda acha o codigo gerado.
    o << "const Entry kEntries[] = {\n";
    std::map<std::uint32_t, std::uint32_t> dono;  // endereco -> funcao que o despacha
    for (const auto &[base, addrs] : enderecos_da_funcao)
        for (std::uint32_t a : addrs)
            if (!dono.count(a)) dono[a] = base;
    for (const auto &[addr, base] : dono) {
        std::uint16_t c = 1;
        auto itc = ciclos_da_funcao.find(base);
        if (itc != ciclos_da_funcao.end()) {
            auto itp = itc->second.find(addr);
            if (itp != itc->second.end()) c = itp->second;
        }
        o << "    {" << hex(addr) << "u, &f_" << std::hex << base << std::dec
          << ", " << c << "u},\n";
    }
    o << "};\n";
    o << "const std::size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);\n";
    // registrar(): a ponte do runtime chama isto quando o jogo carrega o modulo.
    // Sem a funcao o codigo gerado compila mas nunca e usado. O NOME sai junto
    // porque as bases sao atribuidas na ordem de carga, que nao e a de registro:
    // sem o nome a ponte nao consegue dizer qual tabela pertence a qual base.
    o << "\nvoid registrar() {\n";
    o << "    ::ps2::iop::registrar_modulo(" << hex(p.base) << "u, " << hex(p.end)
      << "u, kEntries, kEntryCount, \"" << p.name << "\");\n";
    o << "}\n";
    o << "\n}  // namespace ps2::iop::gen::" << ns << "\n";
    return o.str();
}

std::string generate_table(const Program &p, const std::string &ns) {
    std::ostringstream o;
    o << "#pragma once\n// GERADO — tabela de entradas de " << p.name << "\n";
    o << "#include \"ps2/iop/r3000_native.hpp\"\n\n";
    o << "namespace ps2::iop::gen::" << ns << " {\n";
    o << "extern const Entry kEntries[];\n";
    o << "extern const std::size_t kEntryCount;\n";
    o << "void registrar();\n";
    o << "}\n";
    return o.str();
}

}  // namespace recomp::r3000
