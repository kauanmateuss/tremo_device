# Task 02 — Compilar o FreeRTOS para um núcleo

O executável `tremo_device` usa o Pico SDK para inicialização e periféricos.
O CMake acrescenta o port GCC para RP2040 do clone local. Esse port fornece a
troca de contexto para o Cortex-M0+ e usa o SysTick para marcar o tempo do
scheduler. Nesta etapa o caminho de execução continua sendo
`main → app_init → laço com app_update`. A Task 03 será o primeiro teste
de execução do scheduler na placa.

## Escolhas de configuração

- `configNUMBER_OF_CORES = 1` seleciona um núcleo. Com preempção habilitada,
  o scheduler poderá alternar entre tarefas prontas conforme sua prioridade.
- O tick de 1 kHz dá uma unidade de 1 ms para esperas do kernel. A periodicidade
  de aquisição dos sensores será definida pela aplicação.
- O alvo `FreeRTOS-Kernel-Heap4` inclui o kernel e o alocador que reúne blocos
  livres adjacentes. O limite inicial de 16 KiB serve para objetos do kernel e
  pilhas alocadas dinamicamente. A pilha mínima da tarefa ociosa tem 256
  palavras (1 KiB). São valores iniciais a medir conforme surgirem tarefas.
- A interoperabilidade de esperas e travas do SDK com o RTOS está desativada.
  Assim, as chamadas atuais a `sleep_ms()` usam o SDK mesmo antes de existir
  um scheduler. Ao migrar os periféricos, revisar como cada tarefa espera e
  como acessa recursos compartilhados.

## Compilar e conferir

O CMake espera o clone em `../FreeRTOS-Kernel`. A revisão usada foi
`ca759444e`. Para usar outro local, passe
`-DFREERTOS_KERNEL_PATH=/caminho/para/FreeRTOS-Kernel` na configuração.
Na raiz do projeto, com as ferramentas da instalação da Task 01:

```sh
export PATH="$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$PATH"
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DTREMO_MQTT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/test_capture_validator.py
```

Aceite no PC: gerar `build/tremo_device.uf2`, identificar os objetos de
`tasks.c`, `portable/ThirdParty/GCC/RP2040/port.c` e
`portable/MemMang/heap_4.c` na linkedição de `tremo_device` e passar nas
suítes existentes. O linker pode descartar funções ainda não chamadas.
Também é possível compilar com `TREMO_MQTT=OFF`.

## Conferir na BitDogLab

Conecte a placa por USB segurando BOOTSEL e copie `build/tremo_device.uf2`
para RPI-RP2. Após o reinício, abra a serial USB e navegue até uma avaliação.
O critério é o menu responder às entradas e a avaliação chegar à tela de
resultado normalmente. Para comparar uma sessão de coleta, use o
[roteiro da versão funcional](preparacao-freertos.md).
