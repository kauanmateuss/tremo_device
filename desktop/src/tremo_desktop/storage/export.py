"""Cópia consistente do SQLite e exportação de uma linha por execução."""
import csv
import os
import sqlite3
from contextlib import closing
from pathlib import Path
from tempfile import NamedTemporaryFile

from .repository import Repository


def export_database(source, destination, *, csv_results=False):
    source = Path(source).resolve()
    destination = Path(destination).absolute()
    protected = {Path(str(source) + suffix) for suffix in ("", "-wal", "-shm", "-journal")}
    if destination.resolve() in protected:
        raise ValueError("Escolha um arquivo diferente do banco em uso.")
    if destination.exists():
        raise ValueError("Escolha um nome de arquivo novo para a exportação.")
    with NamedTemporaryFile(dir=destination.parent, delete=False) as tmp:
        staging = Path(tmp.name)
    try:
        if csv_results:
            with closing(Repository(source, read_only=True)) as repo:
                rows = repo.db.execute("""SELECT s.patient_id, r.device AS device_id,
                    r.session AS session_id, r.run_id AS test_run_id, r.run_number, r.module,
                    r.started_at, r.finished_at, r.state, r.data AS result_json
                    FROM test_runs r JOIN sessions s USING(device,session)
                    ORDER BY s.received_first, r.device, r.session, r.run_number, r.module""")
                with staging.open("w", encoding="utf-8-sig", newline="") as stream:
                    writer = csv.writer(stream, delimiter=";")
                    writer.writerow([column[0] for column in rows.description])
                    writer.writerows(rows)
        else:
            with closing(sqlite3.connect(source.as_uri() + "?mode=ro", uri=True)) as db:
                with closing(sqlite3.connect(staging)) as backup:
                    db.backup(backup, pages=256)
        os.link(staging, destination)
    finally:
        staging.unlink(missing_ok=True)
