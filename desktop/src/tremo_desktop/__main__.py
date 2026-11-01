"""Entrada da coleta e consulta local; a interface PySide6 virá em outra etapa."""
import argparse
import json
import logging
import os
from pathlib import Path
import sqlite3
from tremo_desktop.storage.repository import Repository


def main():
    parser = argparse.ArgumentParser(description="Tremo — coleta MQTT e sessões SQLite")
    parser.add_argument("--db", type=Path, default=Path("desktop/tremo.sqlite3"))
    commands = parser.add_subparsers(dest="command", required=True)
    collect = commands.add_parser("collect", help="receber dados até Ctrl+C")
    collect.add_argument("--host", default=os.getenv("TREMO_MQTT_HOST", "192.168.1.215"))
    collect.add_argument("--port", type=int, default=int(os.getenv("TREMO_MQTT_PORT", "1883")))
    collect.add_argument("--client-id", default=os.getenv("TREMO_MQTT_CLIENT_ID", "tremo-collector-local"))
    commands.add_parser("sessions", help="listar sessões recebidas")
    report = commands.add_parser("report", help="consultar métricas e qualidade de uma sessão")
    report.add_argument("device")
    report.add_argument("session")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    repository = None
    try:
        repository = Repository(args.db)
        if args.command == "collect":
            from tremo_desktop.transport.mqtt import Collector
            Collector(repository, args.client_id, os.getenv("TREMO_MQTT_USER"),
                      os.getenv("TREMO_MQTT_PASSWORD")).run(args.host, args.port)
        else:
            data = repository.sessions() if args.command == "sessions" else repository.overview(args.device, args.session)
            print(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False))
    except KeyboardInterrupt:
        logging.info("Coletor encerrado")
    except (OSError, sqlite3.Error, RuntimeError, KeyError, ValueError) as exc:
        logging.error("%s", exc)
        return 1
    finally:
        if repository:
            repository.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
