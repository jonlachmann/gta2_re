#!/usr/bin/env python3
"""Trains the bots with reinforcement learning (PPO, torch) by self-play (docs/netcode.md, "Bots").

Headless arena servers (--arena remote:<port>,...) connect to this script; every 3 ticks each sends what its remote
bots see and the reward since (Port/net/session/arena.cpp) and gets their buttons back. One policy plays all
learner slots; some matches put earlier versions of it (the pool) in two of them, and the scripted "simple" bot
always plays the sixth slot as a yardstick. At first the policy also learns to press what the scripted bot would
(kickstarting, --teach), less with each update. Weapons come from the map's pickups (--armed: pistols for all, a start).

  build_port/rlvenv/bin/python Scripts/port/rl_bots.py [--updates N] [--envs N] [--spectate K]
  build_port/rlvenv/bin/python Scripts/port/rl_bots.py --watch [file.nn]   a match in a window

Out: build_port/rl/ (latest.nn for the game's nn: brain, policy.pt to continue, pool/, log.csv).
Setup: python3.12 -m venv build_port/rlvenv && build_port/rlvenv/bin/pip install torch numpy
"""
import argparse
import glob
import os
import random
import re
import select
import socket
import struct
import subprocess
import sys
import time

import numpy as np
import torch
import torch.nn as nn

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAME = os.path.join(ROOT, "build_port", "mac", "game")
BIN = os.path.join(ROOT, "build_port", "mac", "gta2_port")
OUT = os.path.join(ROOT, "build_port", "rl")
MAP = "mp1-6p"  # Tiny Town
GROUPS = [3, 3, 2, 2, 2, 2]  # bot.hpp's kBotGroupSize: turn, move, attack, enter/exit, jump, next weapon
TURN, MOVE, PRESS = [0x4, 0, 0x8], [0x1, 0, 0x2], [0x10, 0x20, 0x40, 0x100]
RESULT = re.compile(r"net: arena (\d+) (\S+) frags (-?\d+) deaths (\d+) near ([\d.]+) dealt (-?\d+) cars (\d+) pickups (\d+)")
HIDDEN = 128
HEAD = 4  # per remote slot before its features: slot, alive, reward, the teacher's buttons (arena.cpp)


def choices(b):
    """The scripted teacher's buttons as a choice per group"""
    turn = 0 if b & TURN[0] else 2 if b & TURN[2] else 1
    move = 0 if b & MOVE[0] else 2 if b & MOVE[2] else 1
    return np.array([turn, move] + [int(bool(b & bit)) for bit in PRESS], dtype=np.int64)


def buttons(a):
    """Per row of choices (n x 6), its buttons"""
    return np.array(TURN)[a[:, 0]] | np.array(MOVE)[a[:, 1]] | (a[:, 2:] * np.array(PRESS)).sum(axis=1)


class Policy(nn.Module):
    def __init__(self, n_in):
        super().__init__()
        self.actor = nn.Sequential(nn.Linear(n_in, HIDDEN), nn.Tanh(), nn.Linear(HIDDEN, HIDDEN), nn.Tanh(), nn.Linear(HIDDEN, sum(GROUPS)))
        self.critic = nn.Sequential(nn.Linear(n_in, HIDDEN), nn.Tanh(), nn.Linear(HIDDEN, HIDDEN), nn.Tanh(), nn.Linear(HIDDEN, 1))
        self.actor[-1].weight.data.mul_(0.01)  # near-uniform choices at first

    def dists(self, obs):
        return [torch.distributions.Categorical(logits=l) for l in self.actor(obs).split(GROUPS, dim=-1)]

    def act(self, obs):
        with torch.no_grad():
            ds = self.dists(obs)
            a = torch.stack([d.sample() for d in ds], dim=-1)
            logp = sum(d.log_prob(a[:, g]) for g, d in enumerate(ds))
            return a, logp, self.critic(obs).squeeze(-1)

    def evaluate(self, obs, a, teacher):
        ds = self.dists(obs)
        logp = sum(d.log_prob(a[:, g]) for g, d in enumerate(ds))
        ent = sum(d.entropy() for d in ds)
        imitation = -sum(d.log_prob(teacher[:, g]) for g, d in enumerate(ds))  # kickstarting: the teacher's choices
        return logp, ent, imitation, self.critic(obs).squeeze(-1)

    def export(self, path, mode="sample"):
        """As the game's nn: brain reads it (Port/net/session/bot_nn.cpp)"""
        layers = [m for m in self.actor if isinstance(m, nn.Linear)]
        sizes = [layers[0].in_features] + [l.out_features for l in layers]
        with open(path + ".tmp", "w") as f:
            f.write("mlp %s %d %s\n" % (mode, len(layers), " ".join(map(str, sizes))))
            for l in layers:
                wb = torch.cat([l.weight.data, l.bias.data[:, None]], dim=1)
                f.write(" ".join("%.6g" % v for v in wb.flatten().tolist()) + "\n")
        os.replace(path + ".tmp", path)


class Match:
    """A game process, and once it connects its socket and the role of each remote slot"""

    def __init__(self, args, port, idx):
        self.log = os.path.join(OUT, "logs", "match_%d.log" % idx)
        self.pool = random.random() < args.pool_share and bool(glob.glob(os.path.join(OUT, "pool", "*.pt")))
        brains = ",".join(["remote:%d" % port] * args.remote + ["simple"])
        env = dict(os.environ, GTA2_HEADLESS="1", GTA2_SPEED="1000")
        if args.armed:
            env["GTA2_NET_TESTARM"] = "0:99"  # a pistol each, kept loaded
        cmd = [BIN, "--server", "--port", str(20000 + idx % 20000), "--map", MAP, "--players", str(args.remote + 1), "--arena", brains, "--ticks",
               str(args.ticks), "--seed", str(random.randrange(1, 1 << 31))]
        self.proc = subprocess.Popen(cmd, cwd=GAME, env=env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        self.sock = None
        self.opponent = None  # slot -> a pool policy
        self.started = time.time()


def read_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError
        buf += chunk
    return bytes(buf)


def results(log):
    out = {}
    try:
        for m in RESULT.finditer(open(log).read()):
            out[int(m.group(1))] = dict(brain=m.group(2), frags=int(m.group(3)), deaths=int(m.group(4)), cars=int(m.group(7)), pickups=int(m.group(8)))
    except OSError:
        pass
    return out


def load_opponent(n_in):
    p = Policy(n_in)
    p.load_state_dict(torch.load(random.choice(glob.glob(os.path.join(OUT, "pool", "*.pt"))), weights_only=True))
    return p


def ppo_update(policy, opt, batch, args, teach):
    obs, act, logp_old, adv, ret, teacher = batch
    adv = (adv - adv.mean()) / (adv.std() + 1e-8)
    n = len(obs)
    for _ in range(args.epochs):
        for idx in torch.randperm(n).split(args.minibatch):
            logp, ent, imitation, v = policy.evaluate(obs[idx], act[idx], teacher[idx])
            ratio = (logp - logp_old[idx]).exp()
            pg = -torch.min(ratio * adv[idx], ratio.clamp(1 - args.clip, 1 + args.clip) * adv[idx]).mean()
            loss = pg + 0.5 * (v - ret[idx]).pow(2).mean() - args.entropy * ent.mean() + teach * imitation.mean()
            opt.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
            opt.step()
    return pg.item(), ent.mean().item()


def gae(traj, bootstrap, gamma, lam):
    """traj: [(obs, act, logp, value, reward, done)]; returns advantages and returns"""
    adv, last = [0.0] * len(traj), 0.0
    for t in reversed(range(len(traj))):
        v, r, done = traj[t][3:6]
        nv = 0.0 if done else (traj[t + 1][3] if t + 1 < len(traj) else bootstrap)
        delta = r + gamma * nv - v
        last = delta + gamma * lam * (0.0 if done else last)
        adv[t] = last
    return adv, [a + tr[3] for a, tr in zip(adv, traj)]


def train(args):
    os.makedirs(os.path.join(OUT, "logs"), exist_ok=True)
    os.makedirs(os.path.join(OUT, "pool"), exist_ok=True)
    torch.set_num_threads(4)
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", args.port))
    listener.listen(64)
    policy, opt, n_in, update = None, None, None, 0
    pending = {}  # (match id, slot) -> [obs, act, logp, value, reward so far]
    trajs = {}  # (match id, slot) -> finished transitions since the last update
    matches, waiting, launched, stored = {}, [], 0, 0
    finished = []  # results of the matches since the last log line
    viewer = None

    def launch():
        nonlocal launched
        launched += 1
        waiting.append(Match(args, args.port, launched))

    for _ in range(args.envs):
        launch()
    while update < args.updates:
        socks = [m.sock for m in matches.values()]
        readable, _, _ = select.select([listener] + socks, [], [], 5.0)
        for m in list(waiting):
            if m.proc.poll() is not None:  # died before connecting
                print("a game ended before it connected: %s" % m.log, flush=True)
                waiting.remove(m)
                launch()
        if listener in readable:
            conn, _ = listener.accept()
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            m = waiting.pop(0)  # ponytail: the oldest launch is taken to be the one that connected (only stats depend on it)
            m.sock = conn
            matches[conn.fileno()] = m
            readable.remove(listener)
        batch = []  # (match key, slot, obs, opponent)
        for s in readable:
            key = s.fileno()
            m = matches[key]
            try:
                count, done, n_feat = struct.unpack("3I", read_exact(s, 12))
                raw = read_exact(s, count * (HEAD + n_feat) * 4)
            except ConnectionError:
                count, done, n_feat, raw = 0, 1, 0, b""
            if policy is None and n_feat:
                n_in = n_feat
                policy = Policy(n_in)
                if os.path.exists(os.path.join(OUT, "policy.pt")):
                    state = torch.load(os.path.join(OUT, "policy.pt"), weights_only=True)
                    policy.load_state_dict(state["policy"])
                    update = state["update"]
                    print("continuing from update %d" % update, flush=True)
                opt = torch.optim.Adam(policy.parameters(), lr=args.lr)
            rows = np.frombuffer(raw, dtype=np.float32).reshape(count, HEAD + n_feat) if count else np.zeros((0, HEAD), np.float32)
            if m.opponent is None:
                slots = [int(r[:1].view(np.uint32)[0]) for r in rows]
                m.opponent = {sl: None for sl in slots}
                if m.pool and len(slots) >= 2:
                    opp = load_opponent(n_in)
                    for sl in random.sample(slots, 2):
                        m.opponent[sl] = opp
            for r in rows:
                slot, alive = (int(v) for v in r[:2].view(np.uint32))
                reward = float(r[2])
                k = (key, slot)
                if m.opponent.get(slot) is not None:
                    if not done:
                        batch.append((key, slot, r[HEAD:], m.opponent[slot], alive, None))
                    continue
                p = pending.get(k)
                if p is not None:
                    p[4] += reward
                    if done or alive:
                        trajs.setdefault(k, []).append((p[0], p[1], p[2], p[3], p[4], bool(done), p[5]))
                        stored += 1
                        del pending[k]
                if not done:
                    batch.append((key, slot, r[HEAD:], None, alive, choices(int(r[3:4].view(np.uint32)[0]))))
            if done:
                s.close()
                del matches[key]
                for k in [k for k in pending if k[0] == key]:
                    del pending[k]
                if m.proc.wait() == 0:
                    finished.append(results(m.log))
                launch()
        # One forward per policy for everyone waiting, then the answers
        replies = {}
        for who in {id(b[3]): b[3] for b in batch}.values():
            rows = [b for b in batch if b[3] is who]
            net = who or policy
            obs = np.stack([b[2] for b in rows])
            a, logp, v = (t.numpy() for t in net.act(torch.from_numpy(obs)))
            btn = buttons(a)
            for i, (key, slot, o, opp, alive, teacher) in enumerate(rows):
                replies.setdefault(key, {})[slot] = int(btn[i]) if alive else 0
                if opp is None and alive:
                    pending[(key, slot)] = [obs[i], a[i], float(logp[i]), float(v[i]), 0.0, teacher]
        for key, by_slot in replies.items():
            order = [b[1] for b in batch if b[0] == key]
            try:
                matches[key].sock.sendall(struct.pack("%dI" % len(order), *[by_slot[sl] for sl in order]))
            except OSError:
                pass
        if stored >= args.steps:
            update += 1
            obs, act, logp, adv, ret, teacher = [], [], [], [], [], []
            with torch.no_grad():
                for k, traj in trajs.items():
                    p = pending.get(k)
                    boot = policy.critic(torch.from_numpy(p[0][None])).item() if p is not None else 0.0
                    a_, r_ = gae(traj, boot, args.gamma, args.lam)
                    obs += [t[0] for t in traj]
                    act += [t[1] for t in traj]
                    teacher += [t[6] for t in traj]
                    logp += [t[2] for t in traj]
                    adv += a_
                    ret += r_
            batch_t = (torch.from_numpy(np.stack(obs)), torch.from_numpy(np.stack(act)).long(), torch.tensor(logp), torch.tensor(adv, dtype=torch.float32),
                       torch.tensor(ret, dtype=torch.float32), torch.from_numpy(np.stack(teacher)))
            teach = args.teach * max(0.0, 1 - update / args.teach_updates)
            pg, ent = ppo_update(policy, opt, batch_t, args, teach)
            mean_r = sum(t[4] for tr in trajs.values() for t in tr) / max(1, stored)
            trajs.clear()
            stored = 0
            log_update(update, finished, mean_r, ent, args)
            finished.clear()
            policy.export(os.path.join(OUT, "latest.nn"))
            torch.save({"policy": policy.state_dict(), "update": update}, os.path.join(OUT, "policy.pt"))
            if update % args.pool_every == 0:
                torch.save(policy.state_dict(), os.path.join(OUT, "pool", "u%05d.pt" % update))
            if args.spectate and update % args.spectate == 0 and (viewer is None or viewer.poll() is not None):
                viewer = watch_match(os.path.join(OUT, "latest.nn"), wait=False)


def log_update(update, finished, mean_r, ent, args):
    learner, simple = [], []
    for res in finished:
        for r in res.values():
            (simple if r["brain"] == "simple" else learner).append(r)

    def mean(rs, f):
        return sum(r[f] for r in rs) / len(rs) if rs else float("nan")

    line = "update %d: reward/step %.4f, entropy %.2f; per match, the policy's bots: frags %.2f deaths %.2f cars %.2f pickups %.2f; the scripted bot: frags %.2f deaths %.2f (%d matches)" % (
        update, mean_r, ent, mean(learner, "frags"), mean(learner, "deaths"), mean(learner, "cars"), mean(learner, "pickups"), mean(simple, "frags"),
        mean(simple, "deaths"), len(finished))
    print(line, flush=True)
    with open(os.path.join(OUT, "log.csv"), "a") as f:
        f.write("%d,%.5f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n" % (update, mean_r, ent, mean(learner, "frags"), mean(learner, "deaths"), mean(learner, "cars"),
                                                           mean(learner, "pickups"), mean(simple, "frags")))


def watch_match(nn_path, wait=True, ticks=0):
    """Five copies of the policy and the scripted bot, in a window at normal speed, following player 0"""
    brains = ",".join(["nn:" + os.path.abspath(nn_path)] * 5 + ["simple"])
    cmd = [BIN, "--server", "--port", "7699", "--map", MAP, "--players", "6", "--arena", brains, "--seed", str(random.randrange(1, 1 << 31))]
    if ticks:
        cmd += ["--ticks", str(ticks)]
    p = subprocess.Popen(cmd, cwd=GAME, stdout=None if wait else subprocess.DEVNULL, stderr=None if wait else subprocess.DEVNULL)
    if wait:
        p.wait()
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--updates", type=int, default=500)
    ap.add_argument("--envs", type=int, default=32, help="games at once (the trainer, not the games, is the limit: more fill its batches)")
    ap.add_argument("--remote", type=int, default=5, help="slots the trainer plays per game (the last is the scripted bot)")
    ap.add_argument("--ticks", type=int, default=4500, help="per match (30 per second of game time)")
    ap.add_argument("--port", type=int, default=7700)
    ap.add_argument("--steps", type=int, default=16384, help="decisions per update")
    ap.add_argument("--minibatch", type=int, default=4096)
    ap.add_argument("--epochs", type=int, default=4)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--gamma", type=float, default=0.99)
    ap.add_argument("--lam", type=float, default=0.95)
    ap.add_argument("--clip", type=float, default=0.2)
    ap.add_argument("--entropy", type=float, default=0.01)
    ap.add_argument("--pool-every", type=int, default=10, help="updates between copies of the policy kept as opponents")
    ap.add_argument("--pool-share", type=float, default=0.5, help="the matches with two pool opponents")
    ap.add_argument("--teach", type=float, default=1.0, help="the imitation loss's weight at the start (kickstarting from the scripted bot)")
    ap.add_argument("--teach-updates", type=int, default=150, help="updates until the imitation loss is gone")
    ap.add_argument("--armed", action="store_true", help="pistols kept loaded (a start: fights happen sooner), not only the pickups")
    ap.add_argument("--spectate", type=int, default=0, metavar="K", help="a windowed match of the policy every K updates")
    ap.add_argument("--watch", nargs="?", const=os.path.join(OUT, "latest.nn"), metavar="NN", help="watch a match in a window instead")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(GAME, "data")):
        sys.exit("no game in %s: run Scripts/port/run_mac.sh once" % GAME)
    if args.watch:
        watch_match(args.watch)
        return
    train(args)


if __name__ == "__main__":
    main()
