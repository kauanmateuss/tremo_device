#!/usr/bin/env python3
"""Valida um CSV USB MPU de uma sessão sem pausa (não acessa hardware)."""
import argparse
import csv
import json
import math
from pathlib import Path


def analyze(path, period_ms, max_jitter_us=10000):
    groups = {}
    errors = []
    with Path(path).open(encoding="utf-8") as stream:
        for line_number, row in enumerate(csv.reader(stream), 1):
            if not row or row[0] != "MPU":
                continue
            try:
                if len(row) != 13:
                    raise ValueError("registro truncado ou numero incorreto de campos")
                session, sequence, timestamp, sensor, valid, read_us = (
                    int(row[1], 16), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6])
                )
                if not 0 < session <= 0xffffffffffffffff or sensor not in (1, 2) or valid not in (0, 1):
                    raise ValueError("ID, sensor ou validade incorretos")
                if min(sequence, timestamp, read_us) < 0 or sequence % 2 != sensor - 1:
                    raise ValueError("sequencia ou tempo incorreto")
                if valid:
                    if not all(math.isfinite(float(value)) for value in row[7:]):
                        raise ValueError("valor nao finito")
                elif any(row[7:]):
                    raise ValueError("amostra invalida deve ter valores vazios")
                key = f"{session:016x}:{sensor}"
                group = groups.setdefault(key, dict(samples=0, invalid=0, gaps=0, timing_violations=0,
                                                   max_jitter_us=0, max_read_us=0, previous=None))
                previous = group["previous"]
                if previous:
                    delta_seq = sequence - previous[0]
                    if delta_seq <= 0 or timestamp <= previous[1]:
                        raise ValueError("sequencia duplicada/fora de ordem ou tempo nao crescente")
                    group["gaps"] += delta_seq // 2 - 1
                    # Só mede jitter entre amostras consecutivas, não entre perdas.
                    if delta_seq == 2:
                        jitter = abs(timestamp - previous[1] - period_ms * 1000)
                        group["max_jitter_us"] = max(group["max_jitter_us"], jitter)
                        group["timing_violations"] += jitter > max_jitter_us
                elif sequence != sensor - 1:
                    errors.append(f"linha {line_number}: captura comeca no meio da sessao")
                group["previous"] = (sequence, timestamp)
                group["samples"] += 1
                group["invalid"] += not valid
                group["max_read_us"] = max(group["max_read_us"], read_us)
            except (ValueError, OverflowError) as exc:
                errors.append(f"linha {line_number}: {exc}")
    if not groups:
        errors.append("nenhuma amostra MPU encontrada")
    for key, group in groups.items():
        group.pop("previous")
        if group["samples"] < 2:
            errors.append(f"{key}: menos de duas leituras")
        partner = key[:-1] + ("2" if key[-1] == "1" else "1")
        if partner not in groups:
            errors.append(f"{key}: segundo sensor ausente")
    passed = not errors and all(not (g["invalid"] or g["gaps"] or g["timing_violations"]) for g in groups.values())
    return dict(passed=passed, period_ms=period_ms, max_allowed_jitter_us=max_jitter_us, groups=groups, errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--period-ms", type=int, choices=(20, 40), required=True)
    parser.add_argument("--max-jitter-us", type=int, default=10000)
    args = parser.parse_args()
    if args.max_jitter_us < 0:
        parser.error("--max-jitter-us deve ser nao negativo")
    try:
        result = analyze(args.capture, args.period_ms, args.max_jitter_us)
    except (OSError, UnicodeError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
