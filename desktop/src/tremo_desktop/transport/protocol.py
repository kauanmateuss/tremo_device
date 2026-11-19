"""Contrato v1 do firmware. Rejeita mensagens incompletas antes da persistência."""
import json
import math
import re

HEX = re.compile(r"[0-9a-f]{16}\Z")
MODULES = ("mot", "rti", "prm", "pal", "mpu")
MAX_PAYLOAD = 16384
# Tópicos do contrato bidirecional v2
TOPIC_EVENTS = "tremo/v1/devices/{device}/events"
TOPIC_COMMANDS = "tremo/v1/devices/{device}/commands"
TOPIC_CONFIRMATIONS = "tremo/v1/devices/{device}/confirmations"
COMMAND_ASSOCIATE = "associate_patient"
COMMAND_DISASSOCIATE = "disassociate_patient"
CONFIRM_ASSOCIATION = "association_confirmed"
CONFIRM_DISASSOCIATION = "disassociation_confirmed"
ASSOC_STATUS_OK = "ok"
ASSOC_STATUS_ERROR = "error"
ASSOC_STATUS_ALREADY = "already_associated"
ASSOC_STATUS_NOT_ASSOCIATED = "not_associated"


class InvalidMessage(ValueError):
    pass


def integer(value, low=0, high=2**63 - 1):
    if type(value) is not int or not low <= value <= high:
        raise InvalidMessage("inteiro fora do contrato")
    return value


def boolean(value):
    if type(value) is not bool:
        raise InvalidMessage("booleano inválido")


def vector(value, length, low, high):
    if not isinstance(value, list) or len(value) != length:
        raise InvalidMessage("vetor inválido")
    for item in value:
        integer(item, low, high)


def fields(data, required):
    if not isinstance(data, dict) or not set(required) <= data.keys():
        raise InvalidMessage("campos obrigatórios ausentes")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise InvalidMessage("chave JSON duplicada")
        result[key] = value
    return result


def _finite_float(text):
    value = float(text)
    if not math.isfinite(value):
        raise InvalidMessage("número não finito")
    return value


def decode(topic, payload):
    try:
        if len(payload) > MAX_PAYLOAD:
            raise InvalidMessage("mensagem excede limite")
        e = json.loads(payload, object_pairs_hook=_unique_object, parse_float=_finite_float,
                       parse_constant=lambda _: (_ for _ in ()).throw(InvalidMessage("número não finito")))
        fields(e, ("v", "device", "session", "seq", "type", "t_us", "data"))
        integer(e.get("run", 0), 0, 2**32-1)
        if type(e["v"]) is not int or e["v"] != 1:
            raise InvalidMessage("versão não suportada")
        for key in ("device", "session"):
            if not isinstance(e[key], str) or not HEX.fullmatch(e[key]):
                raise InvalidMessage("identificador inválido")
        if int(e["session"], 16) == 0:
            raise InvalidMessage("sessão zero")
        if topic != f'tremo/v1/devices/{e["device"]}/events':
            raise InvalidMessage("dispositivo não corresponde ao tópico")
        integer(e["seq"], 0, 2**32 - 1)
        integer(e["t_us"])
        d, kind = e["data"], e["type"]
        if kind == "start":
            fields(d, ("kind", "duration_ms", "sample_ms", "tests_mask", "tremor_first", "protocol"))
            if d.get("patient_id", "") != "" and not valid_patient(d["patient_id"]):
                raise InvalidMessage("identificador do paciente inválido")
            integer(d["duration_ms"]); integer(d["sample_ms"]); integer(d["kind"], 0, 7); integer(d["tests_mask"], 1, 15); boolean(d["tremor_first"])
            for option in ("mot_mpu", "cognitive_mpu"):
                if option in d:
                    boolean(d[option])
            if e["seq"] != 0 or d["duration_ms"] not in (30000, 60000) or d["sample_ms"] not in (20, 40) or d["protocol"] != "tremo-adaptado-v1":
                raise InvalidMessage("configuração inválida")
        elif kind == "test_start":
            fields(d, ("module",))
            if d["module"] not in MODULES[:4] or not e.get("run"):
                raise InvalidMessage("início de teste inválido")
        elif kind in ("state", "end"):
            fields(d, ("state", "active_us", "paused_us", "pauses", "tx_dropped"))
            integer(d["state"], 1, 5)
            if (kind == "end") != (d["state"] >= 3):
                raise InvalidMessage("estado terminal inconsistente")
            for key in ("active_us", "paused_us", "pauses", "tx_dropped"):
                integer(d[key])
        elif kind == "samples":
            fields(d, ("module", "rows"))
            if d["module"] != "mpu" or not isinstance(d["rows"], list) or not 1 <= len(d["rows"]) <= 2:
                raise InvalidMessage("lote inválido")
            for row in d["rows"]:
                vector(row, 14, -2**31, 2**63 - 1)
                integer(row[0], 0, 2**32 - 1); integer(row[1]); integer(row[2], 1, 2)
                integer(row[3], 0, 1); integer(row[4], 0, 2**32 - 1)
                if row[0] % 2 != row[2] - 1:
                    raise InvalidMessage("sensor/sequência inconsistentes")
                for value in row[5:11]: integer(value, -32768, 32767)
                for value in row[11:]: integer(value, -65535, 65535)
                if not row[3] and any(row[5:]):
                    raise InvalidMessage("amostra inválida com valores")
        elif kind == "trial":
            fields(d, ("module", "index", "training", "outcome"))
            module = d["module"]
            if module not in MODULES[:4]: raise InvalidMessage("teste inválido")
            integer(d["index"], 0, {"mot": 12, "rti": 12, "prm": 7, "pal": 28}[module])
            integer(d["outcome"], 0, 3); boolean(d["training"])
            if module == "rti":
                fields(d, ("hold_us", "onset_us", "release_us", "ended_us", "reaction_us", "planned_wait_us"))
                for key in ("hold_us", "ended_us"): integer(d[key])
                integer(d["planned_wait_us"], 1000000, 3000000)
                for key in ("onset_us", "release_us", "reaction_us"):
                    if d[key] is not None: integer(d[key])
                if d["ended_us"] < d["hold_us"]: raise InvalidMessage("ordem temporal inválida")
                if d["outcome"] == 0:
                    if d["onset_us"] is None or d["release_us"] is None or d["reaction_us"] is None:
                        raise InvalidMessage("resposta sem tempo")
                    if not 0 <= d["reaction_us"] < 2000000 or d["reaction_us"] != d["release_us"] - d["onset_us"]:
                        raise InvalidMessage("latência inválida")
                elif d["reaction_us"] is not None: raise InvalidMessage("latência sem resposta válida")
            else:
                fields(d, ("onset_us", "elapsed_us")); integer(d["onset_us"]); integer(d["elapsed_us"])
                if module == "mot":
                    fields(d, ("target", "selected"))
                    vector(d["target"], 2, 0, 4); vector(d["selected"], 2, 0, 4)
                elif module == "prm":
                    fields(d, ("target", "novel", "target_option", "selected_option"))
                    integer(d["target"], 0, 15); integer(d["novel"], 0, 15); integer(d["target_option"], 0, 1)
                    if d["target"] == d["novel"] or type(d["selected_option"]) is not int or d["selected_option"] not in (0, 1, 255):
                        raise InvalidMessage("alternativa inválida")
                else:
                    fields(d, ("pairs", "round", "trial", "pattern", "target_position", "selected_position"))
                    integer(d["pairs"], 2, 4); integer(d["round"], 1, 3); integer(d["trial"], 1, d["pairs"])
                    integer(d["pattern"], 0, 15); integer(d["target_position"], 0, 3)
                    if type(d["selected_position"]) is not int or d["selected_position"] not in (0, 1, 2, 3, 255):
                        raise InvalidMessage("posição inválida")
        elif kind == "result":
            fields(d, ("module",))
            if d["module"] == "mpu":
                fields(d, ("reason", "duration_ms", "sample_ms", "calibrated", "valid", "errors", "missed_slots", "buffer_drops", "max_lateness_ms", "max_read_us", "max_pair_skew_us", "gyro_bias"))
                integer(d["sample_ms"]); integer(d["reason"], 0, 2); integer(d["duration_ms"]); boolean(d["calibrated"])
                if d["sample_ms"] not in (20, 40): raise InvalidMessage("taxa inválida")
                for key in ("valid", "errors"): vector(d[key], 2, 0, 2**32 - 1)
                for key in ("missed_slots", "buffer_drops", "max_lateness_ms", "max_read_us", "max_pair_skew_us"): integer(d[key])
                if not isinstance(d["gyro_bias"], list) or len(d["gyro_bias"]) != 2: raise InvalidMessage("calibração inválida")
                for row in d["gyro_bias"]: vector(row, 3, -32768, 32767)
            elif d["module"] in MODULES[:4]:
                fields(d, ("interrupted", "correct", "wrong", "omissions", "valid_time_us", "record_count", "levels_completed", "rounds_started", "limit_reached"))
                boolean(d["interrupted"]); boolean(d["limit_reached"])
                for key in ("correct", "wrong", "omissions", "record_count"): integer(d[key], 0, 29)
                integer(d["valid_time_us"]); integer(d["levels_completed"], 0, 3); integer(d["rounds_started"], 0, 9)
                if d["correct"] + d["wrong"] + d["omissions"] > d["record_count"]: raise InvalidMessage("contagens inválidas")
            else: raise InvalidMessage("módulo inválido")
        else:
            raise InvalidMessage("tipo de mensagem desconhecido")
        return e
    except InvalidMessage:
        raise
    except (TypeError, KeyError, ValueError, RecursionError) as exc:
        raise InvalidMessage("mensagem não corresponde ao contrato") from exc


def valid_patient(value):
    return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_-]{1,16}", value) is not None


def encode_command(device, command, data):
    """Codifica um comando para publicação no tópico de comandos."""
    if not HEX.fullmatch(device) or command not in (COMMAND_ASSOCIATE, COMMAND_DISASSOCIATE) or not valid_patient(data.get("patient_id")):
        raise InvalidMessage("Dispositivo, comando ou paciente inválido")
    return dict(v=2, device=device, command=command, data={"patient_id": data["patient_id"]})


def encode_confirmation(device, confirmation, status, data=None):
    """Codifica uma confirmação para publicação no tópico de confirmações."""
    payload = dict(v=2, device=device, confirmation=confirmation, status=status)
    if data is not None:
        payload["data"] = data
    return payload


def decode_command(topic, payload):
    """Decodifica e valida uma mensagem recebida no tópico de confirmações."""
    import json
    import math
    try:
        if len(payload) > MAX_PAYLOAD:
            raise InvalidMessage("mensagem excede limite")
        e = json.loads(payload, object_pairs_hook=_unique_object, parse_float=_finite_float,
                       parse_constant=lambda _: (_ for _ in ()).throw(InvalidMessage("número não finito")))
        fields(e, ("v", "device", "confirmation", "status"))
        if type(e["v"]) is not int or e["v"] != 2:
            raise InvalidMessage("versão não suportada")
        if not isinstance(e["device"], str) or not HEX.fullmatch(e["device"]):
            raise InvalidMessage("identificador inválido")
        if topic != topic_confirmations(e["device"]):
            raise InvalidMessage("dispositivo diverge do tópico")
        if e["confirmation"] not in (CONFIRM_ASSOCIATION, CONFIRM_DISASSOCIATION, "device_state"):
            raise InvalidMessage("confirmação desconhecida")
        offline = e["confirmation"] == "device_state" and e["status"] == "offline"
        if not offline and e["status"] not in (ASSOC_STATUS_OK, ASSOC_STATUS_ERROR, ASSOC_STATUS_ALREADY, ASSOC_STATUS_NOT_ASSOCIATED):
            raise InvalidMessage("status inválido")
        if "data" in e and not isinstance(e["data"], dict):
            raise InvalidMessage("dados devem ser um objeto")
        if "firmware" in e.get("data", {}) and not isinstance(e["data"]["firmware"], str):
            raise InvalidMessage("versão de firmware inválida")
        patient = e.get("data", {}).get("patient_id")
        if not valid_patient(patient) and not (e["confirmation"] == "device_state" and patient == ""):
            raise InvalidMessage("paciente inválido")
        return e
    except InvalidMessage:
        raise
    except (TypeError, KeyError, ValueError, RecursionError) as exc:
        raise InvalidMessage("confirmação não corresponde ao contrato") from exc


def topic_commands(device):
    return TOPIC_COMMANDS.format(device=device)


def topic_confirmations(device):
    return TOPIC_CONFIRMATIONS.format(device=device)
