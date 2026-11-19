"""Coletor independente da GUI. Uma thread executa MQTT e transacoes curtas.

Suporta comunicacao bidirecional:
- assina t_remo/v1/devices/+/events (telemetria da placa)
- assina t_remo/v1/devices/+/confirmations (confirmacoes da placa)
- publica em t_remo/v1/devices/<id>/commands (comandos para a placa)
"""
import logging
import json
from tremo_desktop.services.ingestion import Ingestion
from tremo_desktop.transport.protocol import (decode_command, InvalidMessage,
    topic_commands, topic_confirmations, TOPIC_EVENTS)

LOG = logging.getLogger(__name__)
TOPIC_EVENTS_WILDCARD = "tremo/v1/devices/+/events"
TOPIC_CONFIRMATIONS_WILDCARD = "tremo/v1/devices/+/confirmations"


class Collector:
    def __init__(self, repository, client_id="tremo-collector-local", username=None,
                 password=None, client=None, on_confirmation=None):
        if client is None:
            import paho.mqtt.client as mqtt
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id,
                                 clean_session=False, protocol=mqtt.MQTTv311,
                                 manual_ack=True)
        self.client = client
        self.ingestion = Ingestion(repository)
        self.on_confirmation = on_confirmation
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
            self._fail(f"Broker recusou conexao: {reason_code}")
            return
        # Assina telemetria e confirmacoes com wildcard de dispositivo.
        result, _ = client.subscribe(TOPIC_EVENTS_WILDCARD, qos=1)
        if result != 0:
            self._fail("Nao foi possível solicitar a assinatura MQTT de eventos")
            return
        result, _ = client.subscribe(TOPIC_CONFIRMATIONS_WILDCARD, qos=1)
        if result != 0:
            self._fail("Nao foi possível solicitar a assinatura MQTT de confirmacoes")

    def _subscribed(self, client, userdata, mid, reason_codes, properties):
        # Duas assinaturas sao confirmadas em um unico SUBACK.
        if len(reason_codes) != 2 or any(int(r.value) != 1 for r in reason_codes):
            self._fail("Broker nao autorizou assinatura com QoS 1")
            return
        LOG.info("Coletor pronto: assinaturas QoS 1 ativas em eventos e confirmacoes")

    def _disconnected(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            LOG.warning("Conexao interrompida; aguardando reconexao automatica")

    def _message(self, client, userdata, message):
        # Em erro de disco/banco, a excecao encerra o loop sem PUBACK. Com a
        # sessao persistente, o broker pode reenviar ao reiniciar este coletor.
        status = self.ingestion.receive(message.topic, message.payload, message.qos)
        if status == "rejected":
            LOG.warning("Mensagem rejeitada; motivo registrado em rejected_messages")
        # Tenta processar como confirmacao se nao foi ingestao de evento.
        if status in ("stored", "duplicate", "rejected"):
            try:
                confirmation = decode_command(message.topic, message.payload)
                if self.on_confirmation:
                    self.on_confirmation(confirmation)
            except InvalidMessage:
                # Nao e uma confirmacao; pode ser evento ja processado acima.
                pass
        if message.qos == 1:
            result = client.ack(message.mid, message.qos)
            if result != 0:
                raise RuntimeError("Falha ao confirmar mensagem; reinicie o coletor")

    def publish_command(self, device, command, data):
        """Publica um comando para o dispositivo especificado."""
        from tremo_desktop.transport.protocol import encode_command
        payload = json.dumps(encode_command(device, command, data)).encode()
        topic = topic_commands(device)
        result, mid = self.client.publish(topic, payload, qos=1)
        if result != 0:
            raise RuntimeError(f"Falha ao publicar comando no topico {topic}")
        LOG.info("Comando publicado: %s -> %s (%s)", command, device, topic)
        return mid

    def run(self, host, port=1883):
        self.client.connect_async(host, port, keepalive=30)
        try:
            self.client.loop_forever(retry_first_connection=True)
            if self.failure:
                raise self.failure
        finally:
            self.client.disconnect()
