# Telemetria MQTT — etapa de integração

Objetivo: transmitir sessões, respostas dos quatro testes e amostras dos dois MPUs
para o Mosquitto, preservando a FSM e os módulos de avaliação.

## Arquitetura

`session` e os testes produzem eventos → `telemetry` mantém uma fila em RAM →
`communication` publica via Wi-Fi/MQTT. `main.c` continua apenas inicializando e
atualizando a aplicação. A rede usa a arquitetura `poll` do Pico SDK; `app.c`
suspende a conexão e o processamento da rede durante os testes cognitivos,
retomando nos resultados. Não há aquisição MPU simultânea aos testes cognitivos.
O impacto real do Wi-Fi na aquisição MPU ainda precisa ser medido na placa.

Na tela **PRONTO**, depois da calibração, confira a linha de rede. Para ver a
sessão ao vivo, aguarde **MQTT CONECTADO** antes de apertar A. **REDE AUSENTE**,
**VER SENHA WIFI**, **WIFI SEM IP** e **AGUARDE MQTT** distinguem as etapas da
conexão. É possível iniciar offline. Durante a calibração e aquisição, o firmware
só atende uma conexão MQTT já estabelecida. Se a conexão cair ou não existir,
adia novas tentativas até sair dessa fase; mensagens sem PUBACK continuam na
fila limitada. Nesse caso parte das amostras pode não chegar ao computador.

MQTT usa **QoS 1**, que admite reenvio: a mensagem sai da fila somente após PUBACK.
A identidade `(device, session, seq)` permite eliminar duplicatas no computador.
PUBACK confirma recepção pelo broker, não gravação no SQLite. Não usamos `retain`.

## Configurar e compilar

Na raiz do projeto:

```sh
cp config/network_config.example.h config/network_config.h
# Preencher SSID/senha da rede de 2,4 GHz e autenticação MQTT, se exigida.
cmake -S . -B build -DTREMO_MQTT=ON -DTREMO_NETWORK_CONFIG_DIR="$PWD/config"
cmake --build build -j 4
python3 tests/run_host_tests.py
```

Endereço inicial: `192.168.1.215:1883`. O arquivo local é ignorado pelo Git.
Use IPv4 numérico. Sem SSID, a aplicação continua no modo USB/OLED; para compilar
sem rede use `-DTREMO_MQTT=OFF`. Grave `build/tremo_device.uf2` via BOOTSEL.
Credenciais compiladas ficam no firmware; não compartilhe o UF2 configurado.
Esta etapa usa TCP sem TLS, para a rede local de desenvolvimento.

## Contrato v1

Tópico: `tremo/v1/devices/<id-da-placa>/events`.
Envelope: `v`, `device`, `session`, `seq`, `type`, `t_us`, `data`.
IDs são strings hexadecimais minúsculas de 16 caracteres. `seq` começa em zero e
contabiliza inclusive tentativas descartadas. `t_us` é monotônico desde o boot,
não horário de calendário; não use horário de chegada para medir reação.

| Tipo | Conteúdo |
| --- | --- |
| `start` | Tipo/configuração da sessão e versão do protocolo adaptado |
| `state` / `end` | Estado, tempos, pausas e descartes da transmissão |
| `trial` | Teste, índice, treino, resposta, estímulo e tempos |
| `result` | Contagens e resumo cognitivo ou qualidade/calibração dos MPUs |
| `samples` | Até duas leituras MPU, com valores brutos e giroscópio corrigido |

Cada linha de `samples.rows` contém, nesta ordem:
`seq, t_us, sensor, valid, read_us, ax, ay, az, raw_gx, raw_gy, raw_gz, gx, gy, gz`.
Aceleração: dividir por 16384 para g. Giroscópio: dividir por 131 para graus/s.
Leituras inválidas não representam zero físico. O receptor as armazena como NULL.
Tentativas de treino ficam identificadas e não entram nas métricas de avaliação.

## Limites e critérios de aceitação

- Fila fixa de 96 mensagens de até 767 caracteres; amostras usam até 16 vagas,
  reservando as demais para respostas, resultados e encerramentos. A fila é
  volátil: reset/apagamento da placa perde mensagens ainda não confirmadas.
- Reconexão com espera progressiva, sem bloquear a FSM. Fila cheia descarta novos
  eventos e conta perdas; o receptor detecta lacunas e encerramentos ausentes.
- O coletor deve assinar o tópico **antes** da avaliação. Sessão persistente do
  subscriber permite recuperar mensagens enquanto ele está desconectado, até os
  limites de fila do broker. Persistência do Mosquitto em disco é necessária para
  sobreviver ao reinício do broker. Não há garantia de entrega infinita/offline.
- Critérios automatizados: PUBACK controla remoção; desconexão conserva mensagem;
  pausa cognitiva impede `poll`; timeout/reconexão preservam identidade; todos os
  quatro testes geram JSON válido; mensagens repetidas não duplicam registros.
- Critérios de bancada: conectar à rede, executar os quatro testes e MPU, conferir
  resumos no receptor, desligar/restaurar o broker e medir `missed_slots`, atrasos
  e descartes nas duas taxas. Esses testes físicos continuam pendentes.

Consulte também [validação técnica](validacao-tecnica.md) e
[coletor desktop](../desktop/README.md).
