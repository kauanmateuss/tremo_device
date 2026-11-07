# Task 06 — Gerenciamento de sessões

A TaskSession é a única proprietária de `app`, `session`, `evaluation` e
`assessment`. Os comandos chegam pela fila da TaskInput. Cada atualização
consome um evento ou espera até 5 ms e avança os prazos da máquina de estados.
A espera bloqueia a tarefa no scheduler e termina assim que chega uma entrada.

`main.c` cria as tarefas e inicia o scheduler. `session_task.c/.h` organizam
a execução da aplicação. `input_task_wait()` entrega uma cópia coerente da
entrada. Os módulos existentes preservam início, pausa em fronteira segura,
retomada, cancelamento e finalização. A configuração só muda fora de uma sessão
ativa. Isso mantém as transições em um único lugar e evita mutexes envolvendo
toda a máquina de estados.

## Compilação e testes

Na raiz do projeto:

```sh
export PATH="$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$PATH"
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/run_rtos_tests.py
python3 tests/test_capture_validator.py
```

Grave `build/tremo_device.uf2` via BOOTSEL e abra a serial USB.
Aceite da etapa: navegar pelos quatro testes, iniciar um MOT, pedir pausa durante
um alvo, terminar a tentativa e verificar a pausa no intervalo. A deve retomar e
B deve encerrar. Na coleta de tremores, pause, aguarde 5 s e retome. O tempo
ativo deve excluir a pausa. Confira também cancelamento no preparo e na tela
de configuração da bateria, além do encerramento normal de uma sessão.

Os testes de fluxo síncrono e por entradas externas cobrem essas transições.
A execução física continua dependendo do ensaio na BitDogLab.

# Task 07 — Aquisição MPU independente

A TaskMPU, prioridade 4 e pilha de 768 palavras (3 KiB), atende o agendamento
a cada tick de 1 ms. O módulo de monitoramento mantém o próximo prazo de
amostragem de 20 ou 40 ms. A referência avança pelo período configurado,
contabilizando intervalos perdidos se houver atraso. Cada sensor tem timestamp
antes da leitura e duração da transferência registrados separadamente.

A TaskSession ainda faz o preparo e a calibração antes da aquisição. O mutex
do monitoramento serializa essas operações com o acesso aos sensores. Na captura,
a TaskMPU é a produtora do buffer de 256 amostras e a TaskSession é a consumidora.
O OLED e a USB usam cópias do estado e liberam o mutex antes da saída.
A fila de telemetria permanece acessada pela TaskSession.

A aquisição encerra por prazo ou falha mesmo se a interface demorar a responder.
O resumo é publicado pela TaskSession e preserva o motivo já detectado pela
TaskMPU. A pausa mantém o buffer e as contagens e exclui seu tempo da duração.
Quando o buffer enche, a captura prossegue e incrementa `buffer_drops`.

Os arquivos principais são `mpu_task.c/.h` e `monitoring.c/.h`.
`monitoring_update()` passa a consultar a saúde da captura no firmware RTOS.
O caminho síncrono continua disponível para os testes anteriores.

## Aceite

Use os mesmos comandos de compilação e testes da Task 06.
O teste POSIX de MPU executa a tarefa real com sensores simulados e verifica:

- A contagem cresce durante saídas de OLED e telemetria artificialmente lentas.
- A pausa interrompe as leituras e a retomada reinicia a cadência.
- O prazo encerra a captura sem depender de chamadas da interface.
- Falha de sensor encerra com erro e não reutiliza valores antigos.
- Um consumidor parado enche o buffer e gera descartes contados.

Na placa, faça três coletas de 30 s a 25 Hz e três a 50 Hz. Os critérios são
750 e 1500 amostras válidas por sensor, respectivamente, sem erros, intervalos
perdidos ou descartes. Confira timestamps e jitter com o validador de captura.
Teste pausa de 5 s, retorno e encerramento por B. Registre os valores observados.
Essas contagens e o jitter físico ainda exigem a bancada.
