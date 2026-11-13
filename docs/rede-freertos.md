# Tasks 11 e 12 — Rede e telemetria com FreeRTOS

## Task 11 — Dono único do rádio e do lwIP

A TaskMQTT tem prioridade 1 e pilha de 1536 palavras (6 KiB). Ela inicializa
o CYW43, atende o lwIP e executa as tentativas de conexão. TaskSession consulta
uma cópia do estado de rede. O firmware mantém `pico_cyw43_arch_lwip_poll`
e `NO_SYS=1`: todas as chamadas de rede e callbacks ocorrem na TaskMQTT,
no mesmo núcleo, com atendimento a cada 10 ms. As tarefas de sensores, entrada
e sessão têm prioridade maior.

A TaskSession inicializa a fila e produz telemetria. Um mutex protege seus
índices, metadados e cópias das mensagens. A TaskMQTT copia a mensagem da cabeça
para um buffer próprio antes de publicar. A escrita USB de diagnóstico fica
na TaskSession, evitando mensagens de rede no meio de uma linha de amostras.
O marcador RTOS inclui `net`, com o valor de `communication_status_t`.

A rede continua sendo atendida durante as avaliações e pode tentar reconectar.
As antigas restrições COMM_SUSPENDED/COMM_STREAM_ONLY são mantidas na API para
a execução síncrona dos testes e não bloqueiam a TaskMQTT no firmware.
O heap FreeRTOS passa a 40 KiB. Os buffers estáticos de telemetria e lwIP são
separados desse heap.

Arquivos centrais: `communication.c/.h`, `telemetry.c/.h`,
`session_task.c` e `FreeRTOSConfig.h`. A configuração de credenciais permanece
em `config/network_config.h`, fora do Git.

## Compilar e testar

```sh
export PATH="$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$PATH"
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DTREMO_MQTT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/run_rtos_tests.py
python3 tests/test_capture_validator.py
PYTHONPATH=desktop/src desktop/.venv/bin/python -m unittest discover -s desktop/tests -p 'test_pipeline.py'
```

O teste POSIX de rede verifica o dono das chamadas do driver, produção durante
uma conexão simulada lenta, lotação da fila e recuperação após queda de link.
Ele substitui o rádio e o broker por funções de teste.

Na placa, grave `build/tremo_device.uf2`, abra a serial USB e inicie uma avaliação
com MPU COG ativado. Desligue o ponto de acesso durante o teste e depois religue.
O teste deve continuar respondendo e a captura deve continuar. O estado de rede
deve voltar a conectado. Confira intervalos perdidos e descartes ao final.
Essa validação física e a conexão com o broker real permanecem pendentes.

## Task 12 — Confirmação, pendências e recuperação

As publicações continuam em QoS 1 e sem retain. A mensagem sai da fila apenas
após PUBACK. Cada tentativa recebe uma geração e o callback confere essa
identificação. Uma confirmação antiga ou duplicada não remove outra mensagem.
A conexão também tem identificação para descartar callbacks de conexões antigas.

A espera de conexão ao broker e a espera de PUBACK têm limite de 15 s.
Falhas imediatas de publicação encerram a conexão e respeitam o backoff de
1 até 32 s, preservando a mensagem pendente. A conexão aceita reinicia o
backoff. O receptor desktop elimina duplicatas pela identidade placa/sessão/
sequência e confirma recepção após gravar no SQLite.

Os testes verificam falha por memória, timeout, PUBACK atrasado/duplicado,
conexão sem resposta e retorno após queda. Na bancada, interrompa o Mosquitto
durante uma sessão e restabeleça-o depois. A sessão deve prosseguir e os dados
ainda presentes na fila devem chegar. Compare os contadores de descartes e
os registros no SQLite. O buffer é limitado e não persiste após desligar a placa.
