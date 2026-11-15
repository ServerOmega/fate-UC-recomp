#pragma once

// Gerador de C++ a partir do codigo MIPS do IOP.
//
// A DIFERENCA QUE IMPORTA: codigo vs dados
// ========================================
// Um segmento PT_LOAD de modulo .IRX tem 94,7% de instrucoes validas e 5,3% de
// dados embutidos (strings, jump tables). Traduzir linearmente o segmento
// inteiro gera C++ para bytes que sao "\\x00\\x00Olá" e o resultado nao compila -
// ou pior, compila e executa lixo.
//
// A solucao e andar o grafo de fluxo a partir das entradas (entry point, JAL,
// destinos de branch) e emitir codigo so para o que for alcancavel. O que sobra
// e dado e nao vira instrucao.
//
// MEDIDO nos 20 modulos: 147.752 palavras no segmento, 139.948 com opcode
// valido, 2.588 funcoes alcancaveis.

#include "r3000.hpp"

#include <string>

namespace recomp::r3000 {

// Gera um arquivo .cpp com as funcoes traduzidas.
// `namespace_` e o nome do modulo (ex.: "MCMAN"), usado nos simbolos.
std::string generate_cpp(const Program &p, const std::string &namespace_);

// Gera o header da tabela de entradas (endereco -> funcao), para o dispatch.
std::string generate_table(const Program &p, const std::string &namespace_);

}  // namespace recomp::r3000
