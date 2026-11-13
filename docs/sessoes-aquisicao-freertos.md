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
