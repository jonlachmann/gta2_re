#!/usr/bin/env python3
"""Evolves neural network bots (Port/net/session/bot_nn.cpp) by self-play (docs/netcode.md, "Bots").

Each generation, the population plays arena matches: headless servers with no clients, the game's clock sped up
(--arena, --ticks, GTA2_SPEED), five networks and the scripted "simple" bot per match, several matches at once.
A network's fitness is its frags, deaths, the damage it dealt and how close it kept to the others. The best
survive unchanged; the rest of the next generation are their mutated copies.

  Scripts/port/train_bots.py [--generations N] [--pop N] [--ticks N] [--jobs N] [--unarmed] [--spectate K]
  Scripts/port/train_bots.py --watch [file.nn ...]     a match of these (default: the latest elites) in a window

Out: build_port/train/gen_NNNN.json (the population), best.nn (the newest best), log.csv. A run continues
from the newest generation there. The macOS build (Scripts/port/build_mac.sh), set up by run_mac.sh once.
"""
import argparse
import glob
import json
import math
import os
import random
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAME = os.path.join(ROOT, "build_port", "mac", "game")
BIN = os.path.join(ROOT, "build_port", "mac", "gta2_port")
OUT = os.path.join(ROOT, "build_port", "train")

# bot_nn.cpp's shape: inputs (with a bias), hidden, outputs
N_IN, N_HID, N_OUT = 49, 32, 10
N_W1, N_W2 = N_IN * N_HID, (N_HID + 1) * N_OUT
MAP = "mp1-6p"  # Tiny Town: the players start 20-60 blocks apart (downtown: 100). ponytail: --map when wanted
PER_MATCH = 5  # networks per match; the sixth slot is the scripted bot, a fixed yardstick
RESULT = re.compile(r"net: arena (\d+) (\S+) frags (-?\d+) deaths (\d+) near ([\d.]+) dealt (-?\d+)")


def fitness(r):
    return 10 * r["frags"] - 5 * r["deaths"] + 0.1 * r["dealt"] + 2 * r["near"]


def random_genome():
    return [random.gauss(0, 1 / math.sqrt(N_IN)) for _ in range(N_W1)] + [random.gauss(0, 1 / math.sqrt(N_HID + 1)) for _ in range(N_W2)]


def mutate(g, sigma):
    return [w + random.gauss(0, sigma) if random.random() < 0.2 else w for w in g]  # 1 in 5 weights moved


def write_nn(g, path):
    with open(path, "w") as f:
        f.write("nn %d %d %d\n" % (N_IN, N_HID, N_OUT))
        f.write(" ".join("%.5f" % w for w in g) + "\n")


def play(brains, port, ticks, armed, window=False):
    """One match; per slot its result dict (missing if the game failed)"""
    env = dict(os.environ, GTA2_SPEED="1" if window else "1000")
    if not window:
        env["GTA2_HEADLESS"] = "1"
    if armed:
        env["GTA2_NET_TESTARM"] = "0:99"  # a pistol each, kept loaded
    args = [BIN, "--server", "--port", str(port), "--map", MAP, "--players", str(len(brains)), "--arena", ",".join(brains)]
    if ticks:
        args += ["--ticks", str(ticks)]
    out = subprocess.run(args, cwd=GAME, env=env, capture_output=not window, text=True, timeout=None if window else 600)
    results = {}
    for m in RESULT.finditer(out.stdout or ""):
        results[int(m.group(1))] = dict(frags=int(m.group(3)), deaths=int(m.group(4)), near=float(m.group(5)), dealt=int(m.group(6)))
    return results


def latest_generation():
    files = sorted(glob.glob(os.path.join(OUT, "gen_*.json")))
    if not files:
        return 0, None
    with open(files[-1]) as f:
        data = json.load(f)
    return data["generation"], data["population"]


def evaluate(pop, args, gen):
    """Every network plays args.rounds matches against random others; mean fitness per network"""
    os.makedirs(os.path.join(OUT, "nn"), exist_ok=True)
    paths = []
    for i, g in enumerate(pop):
        paths.append(os.path.join(OUT, "nn", "%d.nn" % i))
        write_nn(g, paths[-1])
    matches = []
    for _ in range(args.rounds):
        order = list(range(len(pop)))
        random.shuffle(order)
        matches += [order[k : k + PER_MATCH] for k in range(0, len(order), PER_MATCH)]
    scores = [[] for _ in pop]
    simple = []

    def run(m):
        idx, members = m
        brains = ["nn:" + paths[i] for i in members] + ["simple"]
        return members, play(brains, 7900 + idx % 64, args.ticks, not args.unarmed)

    with ThreadPoolExecutor(args.jobs) as ex:
        for members, res in ex.map(run, enumerate(matches)):
            for slot, i in enumerate(members):
                if slot in res:
                    scores[i].append(fitness(res[slot]))
            if len(members) in res:
                simple.append(fitness(res[len(members)]))
    fit = [sum(s) / len(s) if s else -1e9 for s in scores]
    return fit, (sum(simple) / len(simple) if simple else float("nan"))


def watch(paths, args):
    if not paths:
        _, pop = latest_generation()
        if not pop:
            sys.exit("no trained generation in %s yet" % OUT)
        os.makedirs(os.path.join(OUT, "watch"), exist_ok=True)
        paths = []
        for i, g in enumerate(pop[:PER_MATCH]):
            paths.append(os.path.join(OUT, "watch", "%d.nn" % i))
            write_nn(g, paths[-1])
    brains = ["nn:" + os.path.abspath(p) for p in paths][:PER_MATCH] + ["simple"]
    print("watching player 0 (%s) against %s" % (brains[0], ", ".join(brains[1:])))
    return play(brains, 7899, args.ticks if args.ticks != 3000 else 0, not args.unarmed, window=True)


def spectate(pop, args):
    """A windowed match of the current elites, beside the training"""
    os.makedirs(os.path.join(OUT, "watch"), exist_ok=True)
    paths = []
    for i, g in enumerate(pop[:PER_MATCH]):
        paths.append(os.path.join(OUT, "watch", "spectate_%d.nn" % i))
        write_nn(g, paths[-1])
    env = dict(os.environ, GTA2_SPEED="1")
    if not args.unarmed:
        env["GTA2_NET_TESTARM"] = "0:99"
    brains = ["nn:" + p for p in paths] + ["simple"]
    return subprocess.Popen([BIN, "--server", "--port", "7898", "--map", MAP, "--players", "6", "--arena", ",".join(brains), "--ticks", "2700"], cwd=GAME, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--generations", type=int, default=50)
    ap.add_argument("--pop", type=int, default=40)
    ap.add_argument("--elites", type=int, default=8)
    ap.add_argument("--rounds", type=int, default=3, help="matches per network per generation")
    ap.add_argument("--ticks", type=int, default=3000, help="per match (30 per second of game time)")
    ap.add_argument("--sigma", type=float, default=0.1, help="the mutation's size")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--unarmed", action="store_true", help="no pistols: only what the map's pickups give")
    ap.add_argument("--spectate", type=int, default=0, metavar="K", help="a windowed match of the elites every K generations")
    ap.add_argument("--watch", nargs="*", metavar="NN", help="watch a match in a window instead of training")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(GAME, "data")):
        sys.exit("no game in %s: run Scripts/port/run_mac.sh once" % GAME)
    if args.watch is not None:
        watch(args.watch, args)
        return
    os.makedirs(OUT, exist_ok=True)
    gen, pop = latest_generation()
    pop = pop or [random_genome() for _ in range(args.pop)]
    viewer = None
    for gen in range(gen + 1, gen + 1 + args.generations):
        fit, simple = evaluate(pop, args, gen)
        ranked = [g for _, g in sorted(zip(fit, pop), key=lambda t: -t[0])]
        best = sorted(fit, reverse=True)
        print("gen %d: best %.1f, elites %.1f, mean %.1f, the scripted bot %.1f" %
              (gen, best[0], sum(best[: args.elites]) / args.elites, sum(fit) / len(fit), simple), flush=True)
        with open(os.path.join(OUT, "log.csv"), "a") as f:
            f.write("%d,%.2f,%.2f,%.2f\n" % (gen, best[0], sum(fit) / len(fit), simple))
        write_nn(ranked[0], os.path.join(OUT, "best.nn"))
        with open(os.path.join(OUT, "gen_%04d.json" % gen), "w") as f:
            json.dump(dict(generation=gen, fitness=best, population=ranked), f)
        if args.spectate and gen % args.spectate == 0 and (viewer is None or viewer.poll() is not None):
            viewer = spectate(ranked, args)
        elites = ranked[: args.elites]
        pop = elites + [mutate(random.choice(elites), args.sigma) for _ in range(len(pop) - args.elites)]


if __name__ == "__main__":
    main()
