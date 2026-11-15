// Gera C++ a partir dos modulos .IRX e reporta o que foi produzido.
//
// Este e o teste que prova o tradutor: nao basta o parser ler, o C++ gerado tem
// que compilar. Se compilar, as instrucoes foram traduzidas para expressoes
// validas; se nao, o gerador esta emitindo lixo.
#include "recomp/r3000.hpp"
#include "recomp/r3000_gen.hpp"

#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

int main(int argc, char **argv) {
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path("C:/Fate/iop-modules");
    const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::path("C:/Fate/generated-iop");

    fs::create_directories(out);

    int ok = 0;
    std::size_t total_codigo = 0;
    for (const auto &e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto nome_arq = e.path().filename().string();
        if (nome_arq.size() < 4 || nome_arq.substr(nome_arq.size() - 4) != ".IRX") continue;

        std::ifstream f(e.path(), std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
        recomp::r3000::Program p;
        p.name = nome_arq;
        std::string erro;
        if (!recomp::r3000::load_elf(bytes, p, erro)) {
            std::printf("%-18s FALHOU: %s\n", nome_arq.c_str(), erro.c_str());
            continue;
        }
        recomp::r3000::find_functions(p);

        // Nome do namespace: sem extensao e sem pontos.
        std::string ns = nome_arq.substr(0, nome_arq.size() - 4);
        std::string cpp = recomp::r3000::generate_cpp(p, ns);

        const auto destino = out / (ns + ".cpp");
        std::ofstream(destino) << cpp;
        ++ok;
        total_codigo += cpp.size();
        std::printf("%-18s %zu palavras -> %zu KB de C++\n", nome_arq.c_str(),
                    p.text.size(), cpp.size() / 1024);
    }
    std::printf("\n%d arquivos gerados, %.0f KB de C++ em %s\n", ok,
                static_cast<double>(total_codigo) / 1024, out.string().c_str());
    return 0;
}
