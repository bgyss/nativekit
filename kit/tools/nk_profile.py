"""
nk_profile: the cost profile's fit, for any console's tool
(kit/docs/kit.md). A console's tool measures its processors on its reference
and names its own kinds of access; this library fits the weights by one
fixed method (non-negative least squares, exact search over which weights
are not zero), reports the error, and fits a cartridge chip's programs.
"""
import itertools, sys


def subset_ls(A, b, idx):
    """least squares on the unknowns in idx alone, from the normal equations;
       None when they do not fix one answer (columns that move together). The
       columns are scaled to a unit diagonal first, so the test is the same
       whatever their units (a chip's fixed part against its op counts)."""
    m = len(idx)
    d = [A[i][i] ** 0.5 for i in idx]
    if any(x <= 0 for x in d):
        return None
    M = [[A[idx[r]][idx[c]] / (d[r] * d[c]) for c in range(m)] + [b[idx[r]] / d[r]] for r in range(m)]
    for c in range(m):
        p = max(range(c, m), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) <= 1e-9:
            return None
        M[c], M[p] = M[p], M[c]
        for r in range(m):
            if r != c:
                k = M[r][c] / M[c][c]
                M[r] = [u - k * v for u, v in zip(M[r], M[c])]
    x = [0.0] * len(b)
    for c, i in enumerate(idx):
        x[i] = M[c][m] / M[c][c] / d[c]
    return x


def fit_exact(A, b, yy):
    """min |Xw - y| for w >= 0, from the normal equations A (X'X), b (X'y) and
       yy (y'y), by exact search over which unknowns are non-zero: the least
       error; among answers with it, the fewest non-zero unknowns; among those,
       the first in the unknowns' order. One answer, so the profile is
       reproducible where the data do not fix one (an op whose count never
       changes is constant work, and goes to the first unknown, a chip's fixed
       part)."""
    n = len(b)
    best, best_sse = [0.0] * n, yy
    found = [(yy, 0, (), [0.0] * n)]
    for size in range(1, n + 1):
        for idx in itertools.combinations(range(n), size):
            x = subset_ls(A, b, list(idx))
            if x is None or any(x[i] < 0 for i in idx):
                continue
            sse = yy - 2 * sum(x[i] * b[i] for i in range(n)) + \
                sum(x[i] * A[i][j] * x[j] for i in range(n) for j in range(n))
            found.append((sse, size, idx, x))
    least = min(f[0] for f in found)
    tol = 1e-9 * max(abs(least), abs(yy), 1.0)
    tied = [f for f in found if f[0] <= least + tol]
    tied.sort(key=lambda f: (f[1], f[2]))
    return tied[0][3]



def fit_weights(frames, measured, defaults, active_clocks):
    """each kind's weight in clocks: non-negative least squares on each frame's
       error in clocks (a frame's time is the sum of its costs, so its slowest
       frames weigh most), over the frames with real work; a kind not measured
       keeps its default"""
    rows = [(f[1], f[2], f[3]) for f in frames if f[2] + f[3] >= active_clocks]
    kk = range(len(measured))
    A = [[0.0] * len(measured) for _ in kk]
    b = [0.0] * len(measured)
    yy = 0.0
    for n, g, bus in rows:
        default = sum(n[k] * defaults[k] for k in kk if not measured[k])
        y = g - default
        yy += y * y
        for i in kk:
            if not measured[i]:
                continue
            b[i] += n[i] * y
            for j in kk:
                if measured[j]:
                    A[i][j] += n[i] * n[j]
    idx = [k for k in kk if measured[k]]
    sub = fit_exact([[A[i][j] for j in idx] for i in idx], [b[i] for i in idx], yy)
    w = [0.0] * len(measured)
    for i, k in enumerate(idx):
        w[k] = sub[i]
    return [w[k] if measured[k] else defaults[k] for k in kk], rows


def errors(rows, weights):
    """bias, mean |error|, 95th percentile, worst; the slowest 5% of frames' bias
       and worst: (predicted - real) / real, real the CPU's work in the frame"""
    def rel(r):
        n, g, bus = r
        pred = bus + sum(w * c for w, c in zip(weights, n))
        return (pred - (g + bus)) / (g + bus)
    if not rows:
        return None
    e = [rel(r) for r in rows]
    a = sorted(abs(x) for x in e)
    heavy = sorted(rows, key=lambda r: r[1] + r[2])[-max(1, len(rows) // 20):]
    he = [rel(r) for r in heavy]
    return (sum(e) / len(e), sum(a) / len(a), a[int(0.95 * (len(a) - 1))], a[-1],
            sum(he) / len(he), max(abs(x) for x in he))


def fmt_err(e):
    if e is None:
        return "no frames with work"
    return ("bias %+.1f%%, mean |error| %.1f%%, p95 %.1f%%, worst %.1f%%; slowest 5%%: bias %+.1f%%, worst %.1f%%"
            % tuple(100 * x for x in e))




def read_jobs(path, tool, header="nk-chip-jobs"):
    jobs, head = {}, None
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == header:
            if int(w[1]) != 1:
                sys.exit("%s: %s: chip jobs format %s, this tool reads 1" % (tool, path, w[1]))
            ops = w[w.index("ops") + 1:w.index("speed")]
            head = (w[2], ops, float(w[w.index("speed") + 1]))
        elif w[0] == "job":
            if not head:
                sys.exit("%s: %s: no %s line first" % (tool, path, header))
            jobs.setdefault(int(w[1], 16), []).append((float(w[2]), [int(x) for x in w[3:3 + len(head[1])]]))
    if not head:
        sys.exit("%s: %s: not a chip jobs file" % (tool, path))
    return head, jobs


def fit_program(jobs, nops):
    """fixed + weights, non-negative least squares on each job's error in
       clocks (the long jobs, the slow frames', weigh most)"""
    n = nops + 1
    A = [[0.0] * n for _ in range(n)]
    b = [0.0] * n
    yy = 0.0
    for clocks, counts in jobs:
        x = [1.0] + counts
        yy += clocks * clocks
        for i in range(n):
            b[i] += x[i] * clocks
            for j in range(n):
                A[i][j] += x[i] * x[j]
    w = fit_exact(A, b, yy)
    return w[0], w[1:]


def chip_errors(jobs, fixed, weights):
    def rel(j):
        clocks, counts = j
        return (fixed + sum(w * c for w, c in zip(weights, counts)) - clocks) / max(clocks, 1.0)
    e = [rel(j) for j in jobs]
    a = sorted(abs(x) for x in e)
    long5 = sorted(jobs, key=lambda j: j[0])[-max(1, len(jobs) // 20):]
    le = [abs(rel(j)) for j in long5]
    return (sum(e) / len(e), sum(a) / len(a), a[int(0.95 * (len(a) - 1))], a[-1], sum(le) / len(le))



def fit_chip(path, tool, min_jobs, header="nk-chip-jobs"):
    (name, ops, speed), jobs = read_jobs(path, tool, header)
    total = sum(c for js in jobs.values() for c, _ in js) or 1.0
    every = [j for js in jobs.values() for j in js]
    mult = sum(c for c, _ in every) / max(1, sum(speed * sum(n) for _, n in every))
    default = (0.0, [mult * speed] * len(ops))
    rows = []
    for entry in sorted(jobs):
        js = jobs[entry]
        if len(js) >= min_jobs:
            fixed, weights = fit_program(js, len(ops))
            how = "fit"
        else:
            m = sum(c for c, _ in js) / max(1, sum(speed * sum(n) for _, n in js))
            fixed, weights, how = 0.0, [m * speed] * len(ops), "multiplier"
        rows.append((entry, fixed, weights, how, len(js), sum(c for c, _ in js) / total,
                     chip_errors(js, fixed, weights)))
    return name, ops, speed, default, rows
