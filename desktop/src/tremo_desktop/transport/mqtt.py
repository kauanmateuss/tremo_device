"""Coletor independente da GUI. Uma thread executa MQTT e transações curtas."""
import logging
from tremo_desktop.services.ingestion import Ingestion

LOG = logging.getLogger(__name__)
TOPIC = "tremo/v1/devices/+/events"


class Collector:
    def __init__(self, repository, client_id="tremo-collector-local", username=None,
                 password=None, client=None):
        if client is None:
            import paho.mqtt.client as mqtt
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id,
                                 clean_session=False, protocol=mqtt.MQTTv311,
                                 manual_ack=True)
        self.client = client
        self.ingestion = Ingestion(repository)
        self.failure = None
        if username:
            client.username_pw_set(username, password)
        client.reconnect_delay_set(min_delay=1, max_delay=30)
        client.on_connect = self._connected
        client.on_subscribe = self._subscribed
        client.on_disconnect = self._disconnected
        client.on_message = self._message

    def _fail(self, message):
        self.failure = RuntimeError(message)
        self.client.disconnect()

    def _connected(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            self._fail(f"Broker recusou conexão: {reason_code}")
            return
        result, _ = client.subscribe(TOPIC, qos=1)
        if result != 0:
            self._fail("Não foi possível solicitar a assinatura MQTT")

    def _subscribed(self, client, userdata, mid, reason_codes, properties):
        if len(reason_codes) != 1 or int(reason_codes[0].value) != 1:
            self._fail("Broker não autorizou assinatura com QoS 1")
            return
        LOG.info("Coletor pronto: assinatura QoS 1 ativa em %s", TOPIC)

    def _disconnected(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            LOG.warning("Conexão interrompida; aguardando reconexão automática")

    def _message(self, client, userdata, message):
        # Em erro de disco/banco, a exceção encerra o loop sem PUBACK. Com a
        # sessão persistente, o broker pode reenviar ao reiniciar este coletor.
        status = self.ingestion.receive(message.topic, message.payload, message.qos)
        if status == "rejected":
            LOG.warning("Mensagem rejeitada; motivo registrado em rejected_messages")
        if message.qos == 1:
            result = client.ack(message.mid, message.qos)
            if result != 0:
                raise RuntimeError("Falha ao confirmar mensagem; reinicie o coletor")

    def run(self, host, port=1883):
        self.client.connect_async(host, port, keepalive=30)
        try:
            self.client.loop_forever(retry_first_connection=True)
            if self.failure:
                raise self.failure
        finally:
            self.client.disconnect()
