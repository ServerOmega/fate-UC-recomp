#pragma once

// Ponte entre o IOP e o codigo gerado pelo tradutor R3000.
//
// Ver r3000_native.cpp para o porque e o como. Resumo do contrato:
//
//   registrar_modulo()  - o jogo chama quando carrega um .IRX traduzido
//   nativo_disponivel() - falso quando nenhum modulo foi registrado
//   rodar_nativo()      - roda o budget usando o codigo compilado, caindo no
//                         interpretador nos enderecos que nao foram traduzidos
//
// A assinatura de `interpretador` e a do laco do IOP: devolve quantos ciclos
// consumiu. A ponte respeita o budget para nao alterar o timing do jogo.

#include <cstdint>

#include "iop/iop.hpp"
#include "ps2/iop/r3000_abi.hpp"

namespace ps2::iop {

// Registra as entradas de um modulo traduzido. `inicio`/`fim` delimitam o
// segmento que o modulo ocupa na memoria do IOP (enderecos virtuais usados pelo
// codigo gerado, tipicamente 0x00000000 + tamanho).
void registrar_modulo(std::uint32_t inicio, std::uint32_t fim,
                      const r3000::Entry *entradas, std::size_t total);

// Ha algum modulo com codigo traduzido?
bool nativo_disponivel();

// Registra todos os modulos gerados. Definido no _registro.cpp gerado.
// Chamar uma vez, no boot do IOP. Quando o arquivo nao existe (build sem IOP
// traduzido), o linker resolve para a versao fraca em r3000_native.cpp.
void registrar_todos();

// Roda ate `budget` ciclos. Usa o codigo gerado nos enderecos que tem entrada e
// chama `interpretador` no resto. Devolve os ciclos consumidos.
void rodar_nativo(Cpu &c, std::int64_t &budget,
                  void (*interpretador)(Cpu &, std::int64_t &));

// Para diagnostico: quantas vezes entrou no codigo gerado e quantas escapou
// para o interpretador. PS2_IOP_NATIVE_STATS=1 imprime no fim.
void estatisticas_nativo(std::uint64_t &chamadas, std::uint64_t &escapadas);

}  // namespace ps2::iop
