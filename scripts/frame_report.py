# -*- coding: utf-8 -*-
"""Analisa o PS2_FRAME_LOG do Fate: onde os milissegundos vao.

FORMATO (de host/window.cpp:1574)
=================================
  frame wall_ms cpu_ms wait_window_ms wait_sound_ms shapes interpreted audio_ms

O QUE PROCURO
=============
O usuario ve quedas para 47 fps nos especiais. A 60 Hz cada quadro tem 16,67 ms.
Se wall_ms passa disso, ha engasgo. As colunas dizem POR QUE:
  - cpu_ms alto     -> a emulacao gastou tempo (EE, VU, IOP, GS)
  - wait_sound_ms   -> esperou o dispositivo de audio esvaziar
  - wait_window_ms  -> esperou a thread da janela (apresentar)
  - shapes alto     -> muitos desenhos naquele quadro (efeito carregado)

Este script separa as causas e mostra os quadros piores com o diagnostico.
"""
import csv
import statistics
import sys
from pathlib import Path

ARQ = Path(sys.argv[1] if len(sys.argv) > 1 else r"C:/Fate/logs/frames.csv")


def main():
    if not ARQ.exists():
        print(f"{ARQ} nao existe")
        return 1

    quadros = []
    with open(ARQ, encoding="latin-1") as f:
        linhas = [l for l in f if l.strip() and not l.startswith("frame")]
    for l in linhas:
        p = l.split()
        if len(p) < 8:
            continue
        try:
            quadros.append({
                "frame": int(p[0]), "wall": float(p[1]), "cpu": float(p[2]),
                "w_win": float(p[3]), "w_snd": float(p[4]),
                "shapes": int(p[5]), "interp": int(p[6]), "audio": float(p[7]),
            })
        except ValueError:
            continue

    if not quadros:
        print("nenhum quadro valido")
        return 1

    n = len(quadros)
    wall = [q["wall"] for q in quadros]
    print(f"=== {n} quadros de {quadros[0]['frame']} a {quadros[-1]['frame']}")
    print(f"wall_ms: media {statistics.mean(wall):.2f}  mediana {statistics.median(wall):.2f}  "
          f"min {min(wall):.2f}  max {max(wall):.2f}")
    print(f"60 Hz pede 16.67 ms por quadro")

    # distribuicao
    acima = lambda lim: sum(1 for w in wall if w > lim)
    for lim, rotulo in ((16.67, "16.67 (=60 fps)"), (17.24, "17.24 (=58 fps)"),
                        (18.18, "18.18 (=55 fps)"), (20.0, "20.0 (=50 fps)"),
                        (21.28, "21.28 (=47 fps)"), (25.0, "25.0 (=40 fps)")):
        c = acima(lim)
        print(f"  acima de {rotulo:22}: {c:5} quadros ({c/n*100:5.2f}%)")

    # causas dos piores
    piores = sorted(quadros, key=lambda q: -q["wall"])[:20]
    print(f"\n=== 20 piores quadros")
    print(f"{'frame':>7} {'wall':>6} {'cpu':>6} {'w_win':>6} {'w_snd':>6} "
          f"{'shapes':>7} {'interp':>6} {'audio':>6}  causa")
    for q in piores:
        # que parcela domina o excesso acima de 16.67
        excesso = q["wall"] - 16.67
        partes = {"cpu": q["cpu"], "som": q["w_snd"], "janela": q["w_win"]}
        causa = max(partes, key=partes.get) if excesso > 0.5 else "-"
        if excesso > 0.5 and partes[causa] < 0.3 * excesso:
            causa = "outro"
        print(f"{q['frame']:>7} {q['wall']:6.2f} {q['cpu']:6.2f} {q['w_win']:6.2f} "
              f"{q['w_snd']:6.2f} {q['shapes']:>7} {q['interp']:>6} {q['audio']:6.0f}  {causa}")

    # correcao: shapes explica o wall?
    print(f"\n=== correlacao entre desenhos e tempo de quadro")
    com_muitos = [q for q in quadros if q["shapes"] > 300]
    com_poucos = [q for q in quadros if q["shapes"] < 50]
    if com_muitos and com_poucos:
        print(f"  quadros com >300 desenhos ({len(com_muitos)}): wall medio "
              f"{statistics.mean([q['wall'] for q in com_muitos]):.2f} ms")
        print(f"  quadros com <50 desenhos  ({len(com_poucos)}): wall medio "
              f"{statistics.mean([q['wall'] for q in com_poucos]):.2f} ms")

    # audio: o wait_sound e o gargalo?
    ws = [q["w_snd"] for q in quadros]
    print(f"\n=== espera pelo som")
    print(f"  media {statistics.mean(ws):.2f} ms  max {max(ws):.2f} ms")
    print(f"  quadros onde a espera pelo som passou de 16 ms: {sum(1 for w in ws if w>16)}")

    interp = sum(q["interp"] for q in quadros)
    print(f"\n=== programas VU interpretados no total: {interp}")
    if interp == 0:
        print("  zero: nenhum efeito caiu no caminho lento")

    return 0


if __name__ == "__main__":
    sys.exit(main())
