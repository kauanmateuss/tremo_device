"""Validação e persistência, sem dependência da biblioteca MQTT ou da futura GUI."""
from tremo_desktop.transport.protocol import decode, InvalidMessage


class Ingestion:
    def __init__(self, repository):
        self.repository = repository

    def receive(self, topic, payload, qos):
        # A transação precisa terminar antes de o transporte confirmar a mensagem.
        try:
            if qos != 1:
                raise InvalidMessage("publicação precisa usar QoS 1")
            event = decode(topic, payload)
            return "stored" if self.repository.ingest(event) else "duplicate"
        except InvalidMessage as exc:
            self.repository.reject(topic, payload, exc)
            return "rejected"
