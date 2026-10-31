# Aplicação desktop — próxima etapa

Fluxo acordado: BitDogLab/Pico W → Wi-Fi/MQTT → Mosquitto no notebook → subscriber
Python (Paho MQTT) → SQLite → interface PySide6/PyQtGraph.

Esta pasta reserva a aplicação desktop. O subscriber, banco e interface ainda
não foram implementados. Sequência de trabalho prevista:

1. Definir mensagens versionadas com device_id, session_id, tipo, sequência e
   timestamps; separar amostras, tentativas, resumo e estado de sessão. Definir
   QoS, limites de fila e política de reconexão antes de ativar publicação.
2. Implementar Wi-Fi e publicador MQTT no firmware, preservando os trechos
   cronometrados e medindo o impacto em perdas/latências.
3. Receber/validar mensagens com Paho MQTT e enfileirar fora da thread gráfica.
4. Persistir em SQLite com transações, migrações e chaves que evitem duplicação.
   Distinguir timestamp monotônico da placa e horário de recepção no notebook.
5. Construir PySide6 e gráficos PyQtGraph consumindo os dados persistidos.

Estrutura proposta para essa implementação:

- `src/tremo_desktop/transport/`: cliente MQTT, reconexão e validação de mensagens.
- `src/tremo_desktop/storage/`: esquema, migrações e repositórios SQLite.
- `src/tremo_desktop/services/`: fila de ingestão e consultas.
- `src/tremo_desktop/ui/`: janelas, modelos e gráficos.
- `tests/`: contratos, armazenamento e integração.

Configuração do broker (IP acessível pela placa, porta e autenticação) e da rede
Wi-Fi ficará fora do Git. O broker já configurado pelo usuário será reutilizado.
O SQLite conterá os registros recebidos; a interface será consumidora desses dados.
