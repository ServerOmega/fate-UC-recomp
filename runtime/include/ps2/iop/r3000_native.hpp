#pragma once

// Ponte publica entre o IOP e o codigo gerado pelo tradutor R3000.
//
// POR QUE ESTE ARQUIVO EXISTE SEPARADO
// ====================================
// O codigo gerado (target ps2game) recebe apenas runtime/include; a ponte vive
// em runtime/src/iop. Sem esta declaracao publica, o gerado nao compila com o
// include path que o CMake do projeto usa.
//
// A implementacao (runtime/src/iop/r3000_native.cpp) tem a logica de busca por
// endereco e o fallback para o interpretador.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ps2/iop/r3000_abi.hpp"

namespace ps2::iop {

// Registra as entradas de um modulo traduzido. Chamado pelo registrar() que o
// gerador emite em cada arquivo de modulo.
void registrar_modulo(std::uint32_t inicio, std::uint32_t fim,
                      const r3000::Entry *entradas, std::size_t total,
                      const char *nome = nullptr);

// Registra pelo NOME do modulo, para o loader chamar quando souber a base real.
// O codigo gerado usa offsets do ELF (base 0), mas o loader copia o modulo para
// um endereco arbitrario da RAM e relocaliza os saltos. A tabela gerada continua
// valendo: a ponte soma a base real ao pc e subtrai para achar a entrada.
void registrar_nome(const char *nome);

// O kernel reescreve codigo em runtime: link_imports e relink_hle_imports trocam
// o stub de uma biblioteca por uma instrucao HLE (opcode 0x3F) em enderecos que o
// ELF tinha como jr $ra (03E00008). Traduzido no build, aquele endereco virou
// "return"; em runtime e HLE. A ponte confere a palavra em memoria antes de usar
// a entrada e, se o opcode for HLE, chama a biblioteca em vez do codigo gerado.
// Sem isso o IOP sobe, os modulos carregam, e a primeira chamada de servico
// devolve lixo (medido: 2 primitivas desenhadas em vez de 182.019).
void modulo_carregado(const std::string &nome, std::uint32_t base);

// Ha algum modulo com codigo traduzido?
bool nativo_disponivel();

// Registra todos os modulos. Definido no _registro.cpp gerado; o runtime tem uma
// versao fraca (vazia) para quando o build nao tem IOP traduzido.
void registrar_todos();

// Roda ate consumir `budget`. Usa o codigo gerado nos enderecos que tem entrada
// e chama `interpretador` no resto. `budget` e por referencia, igual a execute().
void rodar_nativo(Cpu &c, std::int64_t &budget,
                  void (*interpretador)(Cpu &, std::int64_t &));

// Diagnostico: quantas vezes entrou no codigo gerado e quantas escapou.
void estatisticas_nativo(std::uint64_t &chamadas, std::uint64_t &escapadas);

}  // namespace ps2::iop
