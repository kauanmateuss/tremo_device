"""Validacao e persistencia, sem dependencia da biblioteca MQTT ou da futura GUI."""
from tremo_desktop.storage.repository import utc_now
from tremo_desktop.transport.protocol import decode, InvalidMessage
from tremo_desktop.transport.protocol import CONFIRM_DISASSOCIATION, ASSOC_STATUS_OK


class Ingestion:
    def __init__(self, repository):
        self.repository = repository

    def receive(self, topic, payload, qos):
        # A transacao precisa terminar antes de o transporte confirmar a mensagem.
        try:
            if qos != 1:
                raise InvalidMessage("publicacao precisa usar QoS 1")
            event = decode(topic, payload)
            return "stored" if self.repository.ingest(event) else "duplicate"
        except InvalidMessage as exc:
            self.repository.reject(topic, payload, exc)
            return "rejected"

    def process_confirmation(self, confirmation):
        """Aplica o estado confirmado pela placa antes do PUBACK."""
        device = confirmation["device"]
        confirm_type = confirmation["confirmation"]
        status = confirmation["status"]
        data = confirmation.get("data") or {}
        patient_id = data.get("patient_id")

        offline = confirm_type == "device_state" and status == "offline"
        if not offline and status != ASSOC_STATUS_OK:
            return
        repository = self.repository
        with repository.db:
            now = utc_now()
            repository.db.execute("INSERT OR IGNORE INTO devices VALUES(?,?,?,?,?)",
                                  (device, "", "identificado", now, now))
            if offline:
                repository.db.execute("UPDATE devices SET state='offline',updated_at=? WHERE device_id=?", (now, device))
                return
            repository.db.execute("""UPDATE devices SET state='online',firmware_version=coalesce(?,firmware_version),
                updated_at=? WHERE device_id=?""", (data.get("firmware"), now, device))
            if confirm_type == CONFIRM_DISASSOCIATION:
                repository.disassociate_patient_device(patient_id, device)
            elif patient_id:
                repository.db.execute("INSERT OR IGNORE INTO patients(patient_id,created_at) VALUES(?,?)",
                                      (patient_id, now))
                repository.associate_patient_device(patient_id, device)
            else:
                repository.db.execute("DELETE FROM patient_devices WHERE device_id=?", (device,))
