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

# Task 05 — Entradas e saídas em tarefas

A inicialização da USB fica em `main()`. A TaskApp configura os periféricos e
depois libera a TaskInput por notificação. Assim, a leitura começa após a
configuração de GPIO e ADC.

| Tarefa | Prioridade | Pilha | Responsabilidade |
|---|---|---|---|
| TaskInput | 3 | 512 palavras, 2 KiB | GPIO, ADC, debounce e eventos |
| TaskApp | 2 | 2048 palavras, 8 KiB | Fluxo existente, composição das telas, sessões e comunicação |
| TaskDisplay | 2 | 1024 palavras, 4 KiB | Transferências de OLED e matriz |

A TaskInput usa `xTaskDelayUntil` com período de 5 ms, referenciado à ativação
anterior. Se perder esse prazo, cede um tick e reinicia a referência. A pilha
é privada de cada tarefa. A prioridade maior da entrada permite interromper
uma atualização do OLED para capturar os botões.

Cada evento da fila leva o estado validado do botão A, sua borda em microssegundos,
o instante da captura e um contador de leituras. A fila preserva a ordem de até
32 eventos. Quando está vazia, a aplicação recebe a amostra mais recente com
`CONTROL_NONE`, incluindo se o debounce ainda está pendente. Uma seção crítica
curta protege a cópia dessa amostra e a operação de fila, sem executar I/O.

A TaskApp usa `app_update_input()` para consumir a cópia recebida. A entrada
`app_update()` continua disponível para os testes síncronos existentes.
O RTI utiliza a borda guardada na mensagem. Seus prazos só avançam com
`CONTROL_NONE`, após consumir os eventos pendentes, para evitar apresentar
o estímulo ou declarar omissão antes de processar uma soltura já capturada.
Os demais testes continuam usando o instante de processamento para suas
latências, como no fluxo anterior. Suas migrações temporais ocorrerão nas
etapas específicas.

Se a fila encher, o firmware para com `FreeRTOS: fila de entradas cheia`.
Essa política simples torna a falha visível e evita continuar a sessão com
entradas perdidas. Reinicie a placa e registre o cenário que levou à saturação.

A matriz usa o mesmo canal do OLED. `npWrite()` aguarda a transferência física
e o intervalo de reset dos LEDs antes de retornar. Assim, os timestamps de início
de estímulo registrados após essa chamada continuam relacionados à conclusão
da apresentação. Os buffers de composição e a tela salva para pausa pertencem
à TaskApp. A TaskDisplay lê esses buffers enquanto a TaskApp espera.
A inicialização dos periféricos ocorre uma vez na TaskApp antes de seus usos.

O firmware existente usa a matriz de LEDs e não tem driver ativo de buzzer
ou de LED RGB discreto. Esta etapa migrou as saídas utilizadas. Adicionar som
aos testes exigirá definir quando acioná-lo e implementar seu driver.

## Arquivos e integração

- `main.c` cria as tarefas e inicia o scheduler.
- `input_task.h/.c` encapsulam a fila, o snapshot e a leitura periódica.
- `display_task.h/.c` encapsulam fila, mutex e confirmação de saída.
- `display.c` e `matrix.c` encaminham as transferências à TaskDisplay.
- `app.c` e `assessment.c` recebem o estado capturado do botão.
- `rtos_hooks.c` informa falhas de recursos e asserts.
- `FreeRTOSConfig.h` mantém um núcleo e heap de 24 KiB.

O adaptador de FreeRTOS é selecionado por `TREMO_FREERTOS_ENABLED` no alvo CMake.
Os testes dos drivers no PC continuam exercitando as funções síncronas.
A máquina de estados segue na TaskApp nesta transição. A TaskSession será a
etapa seguinte, e os MPUs e a comunicação ainda seguem o fluxo dessa aplicação.

## Validação conjunta das Tasks 03 a 05

Na raiz do projeto, use o PATH da Task 03 e execute:

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DTREMO_MQTT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/run_rtos_tests.py
python3 tests/test_capture_validator.py
```

Os testes do kernel POSIX agora exercitam OLED e matriz com dois produtores e
a TaskInput com o driver real de debounce e GPIO/ADC simulados. Testam também
a saturação da fila. A suíte de fluxo roda novamente pela entrada que recebe
eventos externos, verificando o mesmo comportamento da FSM.

Grave o UF2 final via BOOTSEL e abra a USB serial. Critérios de aceite na placa:

1. Observe por 60 s linhas `# RTOS core=0 tick=... app=... input=... heap=...`.
   Os ticks e o contador de entradas devem crescer, sem panic ou assert.
2. Percorra o menu por 30 s alternando joystick e botões. Um acionamento deve
   produzir uma ação. O joystick precisa voltar ao centro antes da próxima.
3. Abra uma avaliação, pause, retome e encerre. A tela restaurada deve estar
   completa e o fluxo deve continuar respondendo.
4. Conclua um MOT e um RTI. A matriz deve mostrar os estímulos e as respostas
   devem aparecer na USB. No RTI, teste também soltar A antes do verde.
5. Execute a coleta de referência da Task 01 e confira o resultado no desktop.
   Registre o commit, logs e valores observados para comparar a cadência.

O PC valida lógica e comunicação entre tarefas. A inicialização do scheduler
RP2040, o funcionamento da USB e a temporização elétrica dos periféricos ainda
precisam desse ensaio físico. O UF2 desta branch contém as três etapas.
