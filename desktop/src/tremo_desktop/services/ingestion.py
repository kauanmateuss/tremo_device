"""Validacao e persistencia, sem dependencia da biblioteca MQTT ou da futura GUI."""
from tremo_desktop.transport.protocol import decode, InvalidMessage
from tremo_desktop.transport.protocol import (decode_command, CONFIRM_ASSOCIATION,
    CONFIRM_DISASSOCIATION, ASSOC_STATUS_OK, ASSOC_STATUS_ERROR,
    ASSOC_STATUS_ALREADY, ASSOC_STATUS_NOT_ASSOCIATED)


class Ingestion:
    def __init__(self, repository):
        self.repository = repository
        self.last_confirmation = None

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
        """Processa uma confirmacao recebida da placa e atualiza o SQLite.

        Retorna um dicionario com o resultado da operacao.
        """
        device = confirmation["device"]
        confirm_type = confirmation["confirmation"]
        status = confirmation["status"]
        data = confirmation.get("data") or {}
        patient_id = data.get("patient_id")

        self.last_confirmation = confirmation

        if confirm_type == CONFIRM_ASSOCIATION:
            if status == ASSOC_STATUS_OK and patient_id:
                self.repository.associate_patient_device(patient_id, device)
                return {"action": "associated", "patient_id": patient_id, "device": device, "status": status}
            elif status == ASSOC_STATUS_ALREADY:
                return {"action": "already_associated", "patient_id": patient_id, "device": device, "status": status}
            elif status == ASSOC_STATUS_ERROR:
                return {"action": "error", "patient_id": patient_id, "device": device, "status": status}
            return {"action": "unknown", "patient_id": patient_id, "device": device, "status": status}

        if confirm_type == CONFIRM_DISASSOCIATION:
            if status == ASSOC_STATUS_OK and patient_id:
                self.repository.disassociate_patient_device(patient_id, device)
                return {"action": "disassociated", "patient_id": patient_id, "device": device, "status": status}
            elif status == ASSOC_STATUS_NOT_ASSOCIATED:
                return {"action": "not_associated", "patient_id": patient_id, "device": device, "status": status}
            elif status == ASSOC_STATUS_ERROR:
                return {"action": "error", "patient_id": patient_id, "device": device, "status": status}
            return {"action": "unknown", "patient_id": patient_id, "device": device, "status": status}

        return {"action": "unknown", "patient_id": patient_id, "device": device, "status": status}
