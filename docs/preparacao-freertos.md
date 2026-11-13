# Task 01 — Referência para a migração ao FreeRTOS

A referência é a primeira versão funcional integrada à `main`, commit
`47254ea2e2645555bbbb5622de157267729a3ca4` (PR #19). Seu conteúdo coincide com
`develop` em `46066ac3` e inclui as correções do temporizador do lwIP para MQTT
e do zoom e arrasto no dashboard. Mesmo que a branch avance, esse hash permite
recuperar a versão de referência e recompilar o firmware com a configuração de
rede local. O UF2 pode conter credenciais e deve ficar fora do Git.

## Como o firmware funciona hoje

Após inicializar os periféricos, o laço em `main.c` chama `app_update()` e dorme
5 ms. Cada volta inclui o tempo gasto nas operações mais esses 5 ms. Essa espera,
por si só, não garante um período fixo de leitura. A máquina de estados decide
qual atividade avançar em cada volta.

| Responsabilidade | Onde encontrar |
|---|---|
| Menu, controles e transições | `src/app.c`, `src/controls.c` |
| Configuração, ciclo de vida das sessões e coordenação das avaliações | `src/session.c`, `src/evaluation.c`, `src/assessment.c` |
| Regras dos quatro testes e padrões visuais | `src/mot.c`, `src/rti.c`, `src/prm.c`, `src/pal.c`, `src/patterns.c` |
| Calibração, aquisição e buffer dos dois MPUs no I2C0 | `src/monitoring.c`, `src/mpu6050_i2c.c` |
| Telas, OLED no I2C1 e matriz de LEDs via PIO | `src/display.c`, `src/ssd1306_i2c.c`, `src/matrix.c`, `ws2818b.pio` |
| Fila de telemetria, política de rede e publicação | `src/telemetry.c`, `src/communication.c`, `src/app.c` |
| Recepção MQTT, SQLite e interface no computador | `desktop/src/tremo_desktop/` |

A coleta MPU e os testes cognitivos ocupam etapas distintas. A rede usa
`pico_cyw43_arch_lwip_poll` e `pico_lwip_mqtt`. A aplicação suspende o serviço de
rede durante o teste cognitivo ativo. Durante a aquisição atende uma conexão já
estabelecida e adia a reconexão. Uma fila cheia pode descartar dados. São limites
conhecidos para comparar depois da migração. Nenhum defeito novo foi reproduzido
nas suítes executadas. A cadência física dos MPUs e o efeito do Wi-Fi continuam
dependendo de medição na placa.

## Dependências e reprodução

A compilação de referência usou a placa `pico_w`, Pico SDK 2.3.1, toolchain Arm
15_2_Rel1, CMake 4.3.4, Ninja 1.13.2 e picotool 2.3.1. Os testes locais exigem
Python 3 e um compilador C do host. O clone disponível para a próxima etapa é
`../FreeRTOS-Kernel` (maiúsculas importam no Linux), revisão `ca759444e`.

Na raiz do projeto, com o Pico SDK instalado e a configuração local de rede em
`config/network_config.h`, execute os comandos abaixo. Em outra instalação,
use `config/network_config.example.h` como modelo para o arquivo local.

```sh
export PATH="$HOME/.pico-sdk/cmake/v4.3.4/bin:$HOME/.pico-sdk/ninja/v1.13.2:$PATH"
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DTREMO_MQTT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
python3 tests/run_host_tests.py
python3 tests/test_capture_validator.py
```

Critérios verificados no PC: compilação concluída, UF2 gerado, 15 suítes C
aprovadas e verificador de captura aprovado com dados de teste.

O binário para gravar é `build/tremo_device.uf2`. Essa pasta é reutilizada nas
compilações seguintes. Para reproduzir a versão funcional após avançar na
migração, com o trabalho salvo, use `git switch --detach 47254ea2`, repita os
comandos acima e volte à branch anterior com `git switch -`.

## Ensaio comparativo na BitDogLab

1. Conecte a Pico W por USB segurando BOOTSEL. Copie o UF2 gerado para
   o volume RPI-RP2. Após reiniciar, abra a serial USB e salve o log.
2. Faça uma sessão de tremores de 30 s a 25 Hz sem pausa, com os dois MPUs
   calibrados. Para verificar também o desktop, inicie o Mosquitto e o receptor
   conforme [o guia MQTT](mqtt.md) e aguarde a indicação de conexão antes da coleta.
3. Execute um MOT até a tela de resultado. Anote o ID de cada sessão e confira
   se os resultados recebidos no desktop pertencem aos mesmos IDs.
4. Para a coleta, a referência esperada é de 750 leituras por sensor, zero
   erros, perdas e descartes. Confira o resumo final e verifique a captura
   com o comando abaixo. Para o MOT, confira o encerramento e o registro das
   respostas. Anote os valores observados e qualquer divergência junto ao log.

```sh
python3 tools/validate_capture.py captura.csv --period-ms 40 --max-jitter-us 10000
```

A [matriz de bancada](validacao-tecnica.md) detalha esses critérios. Repita o
mesmo ensaio após a integração do FreeRTOS e compare as capturas. A preparação
da migração parte da versão funcional acima.
