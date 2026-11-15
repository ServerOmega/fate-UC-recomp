#include "iop.hpp"
#include "kernel.hpp"
#include "ps2/iop/r3000_native.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Ponte entre o IOP e o codigo gerado pelo tradutor R3000.
//
// COMO FUNCIONA
// =============
// O kernel do IOP chama execute(c, budget) para rodar 512 ciclos. Com a ponte:
//
//   1. Se o pc atual esta numa funcao traduzida, chama a funcao gerada.
//   2. A funcao roda ate um branch/jump/return e devolve o novo pc em c.pc.
//   3. Se o pc devolvido NAO tem entrada, o interpretador roda o resto.
//   4. Se devolve kUnwind, o controle volta ao interpretador sem gastar budget.
//
// CONTAGEM DE CICLOS
// ==================
// O timing do IOP depende do budget ser consumido com precisao: 512 ciclos por
// fatia, e o kernel usa quanto sobrou para avancar o relogio. A ponte conta 1
// por chamada (um bloco ate o primeiro branch). Isso e conservador: sobra
// budget, o kernel roda mais fatias, e o custo extra e um despacho a mais.
//
// POR QUE O FALLBACK E OBRIGATORIO
// ================================
// Medido: a analise estatica alcanca 62,6% das palavras. Os outros 37,4% sao
// dados (5,3%) e codigo alcancado por ponteiro (32,1%) - callbacks registrados
// por endereco, tabelas de salto do kernel. Sem fallback o IOP trava no primeiro
// callback.

namespace ps2::iop {
namespace {

struct Faixa {
    std::uint32_t inicio;      // endereco real (base somada)
    std::uint32_t fim;         // endereco real
    std::uint32_t deslocamento; // base - elf_inicio: somar para achar a entrada
    const r3000::Entry *entradas;
    std::size_t total;
};

// Tabelas por nome de modulo (enderecos do ELF, base 0) e a base real que o
// loader deu a cada um. A busca converte: offset = pc - base_real, e procura o
// offset na tabela.
struct Tabela {
    std::string nome;
    std::uint32_t elf_inicio, elf_fim;
    const r3000::Entry *entradas;
    std::size_t total;
    std::uint32_t base_real;   // 0 enquanto o modulo nao foi carregado
};
std::vector<Tabela> g_tabelas;

Faixa *g_faixas = nullptr;
std::size_t g_n_faixas = 0;
std::size_t g_cap = 0;
bool g_pronto = false;
std::uint64_t g_chamadas = 0;
std::uint64_t g_escapadas = 0;
// Separar os dois tipos de saida da ponte e o que diz se ha emulacao de verdade:
//   g_hle_calls = chamadas de biblioteca do sistema, ja executadas em C++ pelo
//                 runtime (install_libraries/install_io/...). Nao e emulacao.
//   g_escapadas_code = instrucao MIPS do jogo que nao achou entrada e foi para o
//                 interpretador. ISSO e emulacao.
std::uint64_t g_hle_calls = 0;
std::uint64_t g_escapadas_code = 0;

void adicionar(std::uint32_t inicio, std::uint32_t fim, const r3000::Entry *e, std::size_t n,
               std::uint32_t deslocamento = 0) {
    if (!e || !n) return;
    if (g_n_faixas == g_cap) {
        const std::size_t novo = g_cap ? g_cap * 2 : 64;
        auto *p = static_cast<Faixa *>(std::realloc(g_faixas, novo * sizeof(Faixa)));
        if (!p) return;
        g_faixas = p;
        g_cap = novo;
    }
    g_faixas[g_n_faixas++] = {inicio, fim, deslocamento, e, n};
}

const r3000::Entry *procurar(std::uint32_t pc) {
    // Varrer de tras para frente: as faixas registradas pelo loader (enderecos
    // reais) sao as ultimas, e devem vencer as do ELF (base 0) quando as duas
    // cobrem o mesmo pc.
    for (std::size_t n = g_n_faixas; n > 0; --n) {
        const Faixa &f = g_faixas[n - 1];
        if (pc < f.inicio || pc >= f.fim) continue;
        // As entradas tem os offsets do ELF; converter o pc real para offset.
        const std::uint32_t alvo = pc - f.deslocamento;
        std::size_t lo = 0, hi = f.total;
        while (lo < hi) {
            const std::size_t meio = (lo + hi) / 2;
            if (f.entradas[meio].pc <= alvo) lo = meio + 1;
            else hi = meio;
        }
        if (lo == 0) continue;
        const std::size_t idx = lo - 1;
        if (f.entradas[idx].pc != alvo) {
            if (std::getenv("PS2_IOP_NATIVE_DEBUG"))
                std::fprintf(stderr, "[dbg] pc=0x%08X faixa 0x%08X..0x%08X alvo=0x%08X -> mais proximo 0x%08X\n",
                             pc, f.inicio, f.fim, alvo, f.entradas[idx].pc);
            continue;
        }
        return &f.entradas[idx];
    }
    return nullptr;
}

// PS2_IOP_NATIVE_STATS=1: relatorio na saida. Sem isto nao da para saber se a
// ponte esta funcionando ou se tudo escapa para o interpretador.
struct Relatorio {
    ~Relatorio() {
        if (!std::getenv("PS2_IOP_NATIVE_STATS")) return;
        const unsigned long long c = g_chamadas, e = g_escapadas;
        const unsigned long long h = g_hle_calls, ec = g_escapadas_code;
        std::fprintf(stderr, "[iop-native] chamadas ao codigo compilado: %llu\n", c);
        std::fprintf(stderr, "[iop-native] chamadas HLE (kernel em C++): %llu\n", h);
        std::fprintf(stderr, "[iop-native] escapes para instrucao MIPS nao traduzida: %llu\n", ec);
        std::fprintf(stderr, "[iop-native] escapes totais: %llu\n", e);
        if (c + h + ec)
            std::fprintf(stderr, "[iop-native] IOP nativo: %.2f%% (traduzido %.2f%% + kernel %.2f%%)\n",
                         100.0 * double(c + h) / double(c + h + ec),
                         100.0 * double(c) / double(c + h + ec),
                         100.0 * double(h) / double(c + h + ec));
        std::fprintf(stderr, "[iop-native] modulos registrados: %zu\n", g_n_faixas);
    }
};
Relatorio g_relatorio;

}  // namespace

void registrar_modulo(std::uint32_t inicio, std::uint32_t fim, const r3000::Entry *e,
                      std::size_t n, const char *nome) {
    g_tabelas.push_back(Tabela{nome ? nome : "", inicio, fim, e, n, 0});
    adicionar(inicio, fim, e, n);
    g_pronto = g_n_faixas > 0;
}

bool nativo_disponivel() { return g_pronto; }

void registrar_nome(const char *nome) {
    if (!nome) return;
    for (Tabela &tab : g_tabelas)
        if (tab.nome == nome) return;   // ja registrado
    std::fprintf(stderr, "[iop-native] nome sem tabela: %s\n", nome);
}

// O loader chama depois de por o modulo na RAM. A partir daqui a tabela do
// modulo responde pelos enderecos reais.
void modulo_carregado(const std::string &nome, std::uint32_t base) {
    // Variantes do nome que o loader pode passar: "SIO2MAN", "SIO2MAN.IRX",
    // "cdrom0:\MODULES\SIO2MAN.IRX". Comparar so a primeira palavra em maiusculas.
    auto chave = [](std::string s) {
        const auto barra = s.find_last_of("/\\");
        if (barra != std::string::npos) s = s.substr(barra + 1);
        const auto ponto = s.find('.');
        if (ponto != std::string::npos) s = s.substr(0, ponto);
        return s;
    };
    const std::string alvo = chave(nome);
    for (Tabela &tab : g_tabelas) {
        if (tab.base_real) continue;
        if (chave(tab.nome) != alvo) continue;
        tab.base_real = base;
        adicionar(base + tab.elf_inicio, base + tab.elf_fim, tab.entradas, tab.total, base);
        std::fprintf(stderr, "[iop-native] %s -> base 0x%08X (%zu entradas)\n",
                     tab.nome.c_str(), base, tab.total);
        return;
    }
    // Casamento por ORDEM DE CARGA: o loader chama na sequencia em que o jogo
    // pediu os modulos, e as tabelas foram registradas na mesma ordem (alfabetica
    // do disco nao, mas a ordem de registro e estavel). Consumir a proxima tabela
    // ainda sem base e o jeito mais previsivel.
    for (Tabela &tab : g_tabelas) {
        if (tab.base_real) continue;
        tab.base_real = base;
        adicionar(base + tab.elf_inicio, base + tab.elf_fim, tab.entradas, tab.total, base);
        std::fprintf(stderr, "[iop-native] faixa 0x%08X..0x%08X <- base 0x%08X (%zu entradas)\n",
                     tab.elf_inicio, tab.elf_fim, base, tab.total);
        return;
    }
    // Casamento por TAMANHO (fallback): o loader reserva exatamente o tamanho
    // do modulo, e a tabela do ELF tem a mesma extensao.
    Tabela *melhor = nullptr;
    std::uint32_t menor_sobra = 0xFFFFFFFFu;
    for (Tabela &tab : g_tabelas) {
        if (tab.base_real) continue;
        if (tab.elf_inicio != 0) continue;
        const std::uint32_t tam = tab.elf_fim - tab.elf_inicio;
        // O modulo ocupa pelo menos `tam`. Escolher o que deixa menos sobra
        // acima do endereco base.
        if (tam && tam < 0x40000u && tam < menor_sobra) {
            menor_sobra = tam;
            melhor = &tab;
        }
    }
    if (melhor) {
        melhor->base_real = base;
        adicionar(base + melhor->elf_inicio, base + melhor->elf_fim,
                  melhor->entradas, melhor->total, base);
        return;
    }
    // Nome pode vir com extensao ou caminho: tentar a forma curta.
    std::string curto = nome;
    const auto barra = curto.find_last_of("/\\");
    const auto ponto = curto.find('.');
    if (ponto != std::string::npos) curto = curto.substr(0, ponto);
    for (Tabela &tab : g_tabelas) {
        if (tab.nome != curto) continue;
        tab.base_real = base;
        adicionar(base + tab.elf_inicio, base + tab.elf_fim, tab.entradas, tab.total, base);
        return;
    }
}

// Versao fraca: o build sem IOP traduzido usa esta. O _registro.cpp gerado
// define uma versao forte que a substitui no link.
__attribute__((weak)) void registrar_todos() {}

// Roda com o codigo compilado enquanto houver entrada, e entrega o resto ao
// interpretador. `budget` e por referencia, igual a execute() do runtime.
//
// NO ESCAPE, UMA INSTRUCAO POR VEZ
// =================================
// A primeira versao entregava o slice inteiro (512 ciclos) ao interpretador no
// primeiro pc sem entrada. Medido: 0,9% de despacho nativo, porque um salto para
// o meio de uma funcao jogava fora o resto da fatia. Agora o escape roda UMA
// instrucao e tenta o nativo de novo - a ponte fica "grudenta": assim que o pc
// volta a um inicio de bloco traduzido, o nativo retoma. O custo e um despacho
// extra por instrucao interpretada, que e barato perto de perder 512 ciclos.
void rodar_nativo(Cpu &c, std::int64_t &budget, void (*interpretador)(Cpu &, std::int64_t &)) {
    // PS2_IOP_NATIVE_OFF=1 desliga a ponte em runtime. Serve para isolar uma
    // regressao: se o jogo volta a funcionar com a ponte desligada, o problema
    // esta no codigo traduzido ou no despacho, nao no resto do runtime.
    static const bool desligada = std::getenv("PS2_IOP_NATIVE_OFF") != nullptr;
    if (!g_pronto || desligada) {
        interpretador(c, budget);
        return;
    }

    while (budget > 0) {
        const r3000::Entry *f = procurar(c.pc);
        if (!f) {
            // STUB HLE CRIADO EM RUNTIME
            // ==========================
            // O linker do kernel escreve as chamadas de biblioteca na RAM como
            // instrucoes HLE (opcode 0x3F) em enderecos que nao existem no ELF -
            // por isso nao estao na tabela gerada. O interpretador as trata no
            // laco principal. Aqui: se a palavra no pc for HLE, executa o mesmo
            // caminho do interpretador em vez de entregar a fatia inteira.
            std::uint32_t w = 0;
            std::memcpy(&w, ps2::iop::ram_ptr(c.pc), 4);
            if ((w >> 26) == 0x3Fu) {
                r3000::hle(c, w & 0x03FFFFFFu);
                ++g_hle_calls;
                budget -= 1;
                continue;
            }
            // NOP NAO PRECISA DE ATALHO AQUI.
            // Tentei tratar w == 0 direto (c.pc += 4) para nao entregar a fatia ao
            // interpretador por causa de um nop no delay slot de retorno. Quebrou o
            // jogo: 23 primitivas em vez de 182.019. Motivo: no MIPS o par
            // pc/npc tem que andar junto, e o nop pode ser delay slot de um branch
            // cujo npc ja aponta para outro lugar. Deixar o interpretador tratar
            // (o escape custa 840 de 76 mil = 1,1%) e o certo.
            if (!g_escapadas) {
                std::fprintf(stderr, "[iop-native] primeiro escape em pc=0x%08X (palavra %08X)\n", c.pc, w);
            }
            ++g_escapadas;
            ++g_escapadas_code;
            // ESCAPE ENTREGA A FATIA INTEIRA. Testado o oposto (lote de 1 e de
            // 64 instrucoes): o jogo TRAVA, porque ~100 mil reentradas por quadro
            // no decodificador de blocos custam mais que os 512 ciclos ganhos.
            interpretador(c, budget);
            return;
        }
        ++g_chamadas;
        const std::uint32_t antes = c.pc;
        // O KERNEL PODE TER TROCADO ESTE ENDERECO POR HLE
        // ==============================================
        // link_imports/relink_hle_imports escrevem (0x3F << 26) | id por cima do
        // que o ELF tinha (normalmente jr $ra). O codigo gerado daquele endereco
        // faz "return"; em runtime a instrucao e uma chamada de biblioteca. Sem
        // esta conferencia o jogo chama a funcao errada e o IOP responde lixo.
        std::uint32_t palavra = 0;
        std::memcpy(&palavra, ps2::iop::ram_ptr(c.pc), 4);
        if ((palavra >> 26) == 0x3Fu) {
            r3000::hle(c, palavra & 0x03FFFFFFu);
            budget -= 1;
            if (c.pc == antes) break;
            continue;
        }
        f->fn(c, 0);
        if (c.pc == r3000::kUnwind) {
            c.pc = antes;
            interpretador(c, budget);
            return;
        }
        // TROCA DE CONTEXTO PENDENTE
        // ==========================
        // O interpretador verifica kernel::switch_pending() a cada instrucao
        // (NEXT() em cpu.cpp) e sai do laco quando o kernel quer trocar de
        // thread. Sem esta checagem a ponte continua executando o bloco seguinte
        // na thread ERRADA: o escalonador marca a troca, o codigo gerado roda
        // mais um bloco antes de devolver, e o estado de duas threads se mistura.
        // Sintoma no jogo: IOP sobe, modulos carregam, primeira chamada de servico
        // devolve lixo (2 primitivas em vez de 182.019).
        if (kernel::switch_pending()) {
            interpretador(c, budget);
            return;
        }
        // COBRAR OS CICLOS DO BLOCO, nao 1. Cada funcao gerada executa `ciclos`
        // instrucoes ate devolver o controle; contar 1 fazia o IOP rodar ~28x
        // adiantado (737 mil ciclos por quadro em vez de 26 mil), o que parou o
        // chip de som e levou o sync_iop a 58 ms.
        //
        // PS2_IOP_ONE_CYCLE=1 volta a cobrar 1 por bloco, para comparar.
        static const bool um_por_bloco = std::getenv("PS2_IOP_ONE_CYCLE") != nullptr;
        budget -= um_por_bloco ? 1 : (f->ciclos ? f->ciclos : 1);
        if (c.pc == antes) break;  // nao andou: evita laco infinito
    }
}

void estatisticas_nativo(std::uint64_t &chamadas, std::uint64_t &escapadas) {
    chamadas = g_chamadas;
    escapadas = g_escapadas;
}

namespace r3000 {

// SYSCALL e BREAK do codigo gerado. O interpretador os trata no laco principal
// (cpu.cpp: op_syscall chama kernel::syscall com o PC da instrucao). No codigo
// gerado o PC ja foi avancado, entao a instrucao esta em c.pc - 4.
// O endereco vem do gerador: dentro de um bloco o switch cai em fall-through e
// c.pc fica no inicio do bloco. Usar c.pc - 4 dava o endereco errado.
void syscall(Cpu &c, std::uint32_t pc) { kernel::syscall(c, pc); }

void breakpoint(Cpu &c, std::uint32_t pc) { std::fprintf(stderr, "[iop] break at 0x%08X\n", pc); }

void hle(Cpu &c, std::uint32_t id) {
    // Mesma sequencia do interpretador (cpu.cpp, op_hle): o retorno esta em ra,
    // entao pc = ra e npc = ra + 4 ANTES de chamar o kernel. Fazer pc = ra + 4
    // (como na primeira versao) pulava uma instrucao do chamador e deixava r2 com
    // 0xFFFFFF9A em vez de 0 - era isso que quebrava o jogo.
    c.pc = c.r[31];
    c.npc = c.r[31] + 4;
    c.r[0] = 0;
    call_hle(c, id);
    c.r[0] = 0;
}

}  // namespace r3000

}  // namespace ps2::iop
