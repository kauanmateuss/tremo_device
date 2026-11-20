# Telemetria MQTT

## Arquitetura

A tarefa de sessão produz eventos e a tarefa de rede os publica via Wi-Fi/MQTT a partir de uma fila em RAM. O atendimento da rede ocorre em paralelo à aquisição e às avaliações. Consulte a [integração com FreeRTOS](rede-freertos.md).

Para acompanhar uma avaliação ao vivo, aguarde a conexão do coletor e da placa ao broker. A tela **PRONTO** informa o estado da rede. A placa permite iniciar offline, mas a capacidade da fila limita a quantidade de eventos que podem ser recuperados.

A telemetria usa QoS 1. O PUBACK confirma o recebimento pelo broker e libera a mensagem da fila. O coletor usa a identidade `(device, session, seq)` para reconhecer reenvios.

## Configuração da rede

Siga as [instruções de compilação](../README.md). No arquivo local `config/network_config.h`, informe o IPv4 do broker acessível pela placa e a porta TCP (por padrão, 1883). O firmware usa Wi-Fi de 2,4 GHz e MQTT sem TLS. As credenciais são incorporadas ao UF2 durante a compilação. Para compilar apenas com USB e OLED, use `-DTREMO_MQTT=OFF`.

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

- Fila fixa de 96 mensagens; amostras usam até 16 vagas,
  reservando as demais para respostas, resultados e encerramentos. A fila é
  volátil: reset/apagamento da placa perde mensagens ainda não confirmadas.
- Reconexão com espera progressiva, sem bloquear a FSM. Fila cheia descarta novos
  eventos e conta perdas; o receptor detecta lacunas e encerramentos ausentes.
- O coletor deve assinar o tópico **antes** da avaliação. Sessão persistente do
  subscriber permite recuperar mensagens enquanto ele está desconectado, até os
  limites de fila do broker. Persistência do Mosquitto em disco é necessária para
  sobreviver ao reinício do broker. Não há garantia de entrega infinita/offline.
- Critérios automatizados: PUBACK controla remoção; desconexão conserva mensagem;
  timeout/reconexão preservam identidade; todos os
  quatro testes geram JSON válido; mensagens repetidas não duplicam registros.
- Critérios de bancada: conectar à rede, executar os quatro testes e MPU, conferir
  resumos no receptor, desligar/restaurar o broker e medir `missed_slots`, atrasos
  e descartes nas duas taxas. Esses testes físicos continuam pendentes.

Consulte também [validação técnica](validacao-tecnica.md) e
[coletor desktop](../desktop/README.md).
