# Task 03 — Partida do scheduler

A TaskApp inicializa a aplicação e executa uma atualização a cada volta, cedendo
o processador com `vTaskDelay(5 ms)`. A TaskStatus escreve um marcador na USB
a cada segundo. Ambas executam no núcleo 0. O scheduler escolhe a tarefa pronta
de maior prioridade. Uma tarefa bloqueada numa espera libera tempo para as
outras tarefas e para a tarefa ociosa.

A TaskApp tem prioridade 2 e pilha de 2048 palavras (8 KiB). A TaskStatus usa
prioridade 1 e 512 palavras (2 KiB). O tick é de 1 ms. O heap de 16 KiB deve
acomodar essas tarefas e as tarefas internas. Os retornos de criação são
verificados e os hooks interrompem a execução em falhas de alocação, assert
ou detecção de estouro de pilha.

A interoperabilidade de sincronização do Pico SDK foi ativada para suas travas
distinguirem tarefas. O port usa um Event Group e a tarefa de timers internamente
para acordar tarefas a partir de interrupções, por isso timers e
`INCLUDE_xTimerPendFunctionCall` estão habilitados. As esperas da aplicação
usam `vTaskDelay`, enquanto as esperas curtas dos drivers continuam pelo SDK.

## Compilar e validar na placa

Na raiz do projeto:

```sh
export PATH="$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$PATH"
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/test_capture_validator.py
```

Grave `build/tremo_device.uf2` via BOOTSEL e abra a serial USB. Aceite da Task 03:

- Linhas `# RTOS core=0 tick=...` continuam chegando por pelo menos 60 s,
  com ticks crescentes e intervalo aproximado de 1 s.
- O menu responde enquanto chegam os marcadores. Um teste cognitivo termina.
- Não aparecem mensagens de falha do FreeRTOS.

A compilação e os testes no PC não executam o scheduler do RP2040. Este ensaio
físico precisa ser feito na placa. Para reproduzir especificamente esta etapa,
use o commit da branch `feature/inicializacao-tarefas-freertos`.
