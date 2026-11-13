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

# Task 04 — Fila, notificação e mutex

A TaskDisplay substitui a tarefa de diagnóstico e atende os pedidos de desenho
do OLED. A TaskApp continua compondo as telas e escreve o diagnóstico após
`app_update()`, evitando inserir marcadores entre os campos de um registro USB.

O caminho é `display_render → fila → TaskDisplay → I2C → notificação → retorno`.
A fila copia um pedido pequeno com os endereços do buffer, da área e do resultado.
O chamador espera a confirmação antes de reutilizar esses dados. A notificação
de índice 0 da TaskApp fica reservada para essa confirmação. O mutex serializa
a transação inteira e usa herança de prioridade do FreeRTOS. A fila de uma
posição é suficiente porque o mutex permite apenas uma transferência pendente.

O driver I2C mantém seu prazo para concluir ou retornar erro. A espera no canal
libera a CPU para outras tarefas. A composição dos buffers continua pertencendo
à TaskApp. A configuração inicial do OLED ocorre antes do primeiro pedido.
O heap passa a 24 KiB para acomodar a TaskDisplay e a próxima tarefa de entradas.

O Event Group usado internamente pelo port atende às travas do SDK.
Os eventos da aplicação desta etapa cabem em filas e notificações.

Aceite na placa: navegar repetidamente pelos menus, abrir e fechar a pausa e
concluir um teste. O OLED deve mostrar telas completas, a USB deve continuar
emitindo `# RTOS core=0 tick=... app=... heap=...` e nenhum assert deve ocorrer.
A perda do OLED deve retornar pelo timeout do driver, permitindo continuar
recebendo o diagnóstico. Desligue a placa antes de alterar as ligações.

Para testar a fila e as confirmações no PC com o kernel real, execute
`python3 tests/run_rtos_tests.py` (Linux, compilador C e pthreads). O teste
usa o port POSIX, dois produtores e uma saída I2C simulada, incluindo erro de
transferência. Ele verifica que cada pedido é atendido pela tarefa de saída,
que o buffer permanece válido e que o resultado chega ao produtor correto.
