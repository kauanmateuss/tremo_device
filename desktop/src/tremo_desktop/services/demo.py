"""Dados fictícios somente para o modo --demo, em um banco temporário separado."""
import json
import math
from datetime import datetime, timedelta, timezone
from tremo_desktop.storage.repository import Repository
from tremo_desktop.transport.protocol import decode


def populate_demo(path):
    repository = Repository(path)
    device = "0123456789abcdef"
    topic = f"tremo/v1/devices/{device}/events"
    try:
        for session_number, kind, state in ((1, 6, 3), (2, 0, 4), (3, 2, 1)):
            session = f"{session_number:016x}"
            seq = 0
            timestamp = 1000000

            def emit(event_type, data, *, drop=False):
                nonlocal seq
                event = dict(v=1, device=device, session=session, seq=seq,
                             type=event_type, t_us=timestamp, data=data)
                seq += 1
                if not drop:
                    repository.ingest(decode(topic, json.dumps(event).encode()))

            emit("start", dict(patient_id="PAC-DEMO", kind=kind, duration_ms=30000, sample_ms=40,
                               tests_mask=15, tremor_first=False, protocol="tremo-adaptado-v1"))
            if kind == 6:
                for module, training_count, test_count in (("mot", 3, 10), ("rti", 3, 10), ("prm", 2, 6), ("pal", 2, 9)):
                    correct = wrong = omissions = valid_time = 0
                    for index in range(training_count + test_count):
                        training = index < training_count
                        trial = index - training_count
                        outcome = 1 if module != "pal" and not training and trial == 3 else 2 if module != "pal" and not training and trial == 5 else 0
                        elapsed = (310 + (index * 37) % 200) * 1000
                        if module != "rti":
                            elapsed += 800000
                        if not training:
                            correct += outcome == 0
                            wrong += outcome == 1
                            omissions += outcome == 2
                            valid_time += elapsed if outcome == 0 else 0
                        data = dict(module=module, index=index, training=training, outcome=outcome)
                        if module == "rti":
                            onset = timestamp + 1500000
                            data.update(hold_us=timestamp, onset_us=None if outcome == 1 else onset,
                                        release_us=timestamp + 500000 if outcome == 1 else onset + elapsed if outcome == 0 else None,
                                        ended_us=timestamp + 500000 if outcome == 1 else onset + elapsed if outcome == 0 else onset + 2000000,
                                        reaction_us=elapsed if outcome == 0 else None, planned_wait_us=1500000)
                        else:
                            data.update(onset_us=timestamp, elapsed_us=elapsed)
                            if module == "mot":
                                data.update(target=[2, 2], selected=[1, 2] if outcome == 1 else [2, 2])
                            elif module == "prm":
                                data.update(target=index, novel=index + 8, target_option=0,
                                            selected_option=0 if outcome == 0 else 1 if outcome == 1 else 255)
                            else:
                                pairs = 2 if training or trial < 2 else 3 if trial < 5 else 4
                                local_trial = index if training else trial if trial < 2 else trial - 2 if trial < 5 else trial - 5
                                data.update(pairs=pairs, round=1, trial=local_trial + 1, pattern=index,
                                            target_position=0, selected_position=0 if outcome == 0 else 1 if outcome == 1 else 255)
                        emit("trial", data)
                        timestamp += 4000000
                    emit("result", dict(module=module, interrupted=False, correct=correct, wrong=wrong,
                                        omissions=omissions, valid_time_us=valid_time,
                                        record_count=training_count + test_count,
                                        levels_completed=3 if module == "pal" else 0,
                                        rounds_started=3 if module == "pal" else 0,
                                        limit_reached=False))
            if kind in (0, 6):
                count = 750 if state == 3 else 250
                start = timestamp
                for index in range(count):
                    timestamp = start + index * 40000
                    rows = []
                    for sensor in (1, 2):
                        phase = index * .04 * 2 * math.pi
                        gx = round(650 * math.sin(4.5 * phase + sensor))
                        gy = round(330 * math.sin(4.5 * phase + .8 * sensor))
                        gz = round(150 * math.cos(4.5 * phase))
                        rows.append([index * 2 + sensor - 1, timestamp + sensor * 200, sensor, 1, 200,
                                     round(280 * math.sin(phase)), round(180 * math.cos(phase)),
                                     16384 + round(120 * math.sin(2 * phase)), gx + 8, gy - 4, gz, gx, gy, gz])
                    emit("samples", dict(module="mpu", rows=rows), drop=state == 4 and 100 <= index < 120)
                timestamp = start + count * 40000
                emit("result", dict(module="mpu", reason=0 if state == 3 else 1, duration_ms=count * 40,
                                    sample_ms=40, calibrated=True, valid=[count, count], errors=[0, 0],
                                    missed_slots=0, buffer_drops=0, max_lateness_ms=2, max_read_us=200,
                                    max_pair_skew_us=200, gyro_bias=[[8, -4, 0], [8, -4, 0]]))
            if state != 1:
                emit("end", dict(state=state, active_us=timestamp - 1000000, paused_us=0, pauses=0,
                                 tx_dropped=20 if state == 4 else 0))
            # Datas separadas tornam a lista legível; nenhum identificador de paciente é inventado.
            received = (datetime.now(timezone.utc) - timedelta(days=session_number - 1)).isoformat()
            with repository.db:
                repository.db.execute("UPDATE sessions SET received_first=?,received_last=? WHERE device=? AND session=?",
                                      (received, received, device, session))
    finally:
        repository.close()
