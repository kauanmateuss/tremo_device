# Tasks 13 e 14 — Integração e medições

## Task 13 — Testes de concorrência

`python3 tests/run_rtos_tests.py` executa o kernel FreeRTOS POSIX com as
tarefas de sessão, entrada, display, MPU e MQTT juntas. As implementações de
máquina de estados, debounce, buffers e telemetria são as do firmware.
GPIO, ADC, I2C, matriz e rádio são substituídos por funções de teste.

O cenário integrado navega pelo menu usando níveis de GPIO/ADC, habilita MPU
COG, calibra, inicia um MOT, pausa e retoma, responde ao primeiro alvo, simula
queda de rede, contabiliza perdas, encerra a avaliação e recupera o envio.
Também confere o ID comum dos registros e a ausência de descarte no buffer
MPU nesse cenário. As suítes isoladas cobrem os quatro protocolos completos,
erros de sensor, saturação de filas, callbacks atrasados e saída lenta.

O teste opt-in de Mosquitto real foi ignorado pelo próprio roteiro porque
esta máquina tem o perfil AppArmor e nenhum `TREMO_TEST_PORT` de broker de
teste administrado externamente foi informado. O teste simulado de rede e os
testes do receptor/SQLite foram executados. O broker de uso normal foi preservado.

## Roteiro obrigatório na placa

1. Grave `build/tremo_device.uf2` via BOOTSEL e salve a saída da USB.
2. Execute MOT, RTI, PRM e PAL com MPU COG ativado, individualmente e na bateria.
3. Em cada cenário, teste pausa, retomada e cancelamento. Confira IDs, motivo
   de término e registros dos sensores e respostas.
4. Faça coletas isoladas de 30 s a 25 e 50 Hz. Espere 750/1500 amostras válidas
   por sensor, zero erros, zero intervalos perdidos e zero descartes.
5. Interrompa o Wi-Fi e o broker separadamente durante uma avaliação. A interface
   e os sensores devem prosseguir. Ao recuperar a conexão, confira os dados
   pendentes no SQLite e os contadores de perdas.
6. Compare o RTI com uma referência elétrica/óptica. A meta inicial é erro
   absoluto de até 10 ms. Registre pelo menos 30 respostas.

```sh
python3 tools/validate_capture.py captura.csv --period-ms 40 --max-jitter-us 10000
```

A aprovação em bancada exige resultados medidos e logs. Os testes do PC não
medem latência de I2C, rádio, USB ou emissão óptica da BitDogLab.
