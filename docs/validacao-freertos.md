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

## Task 14 — Instrumentação e critérios de ajuste

A cada 5 s fora de uma sessão ativa, a USB informa:

- `RTOS_MEM heap_free`: bytes livres no heap FreeRTOS de 40 KiB.
- `heap_min`: menor quantidade livre desde o boot.
- `RTOS_STACK free_min_bytes`: menor folga observada na pilha de cada tarefa.

A varredura das pilhas ocorre fora de sessões ativas para reduzir interferência
nas medições. Esses valores não representam toda a RAM da placa: buffers
estáticos e o heap usado pelo SDK são separados. O mapa do linker e o tamanho
do ELF complementam a inspeção.

A configuração inicial mantém TaskMPU em prioridade 4, TaskInput em 3,
TaskSession e TaskDisplay em 2, TaskMQTT em 1. Pilhas: MPU 3 KiB, Input 2 KiB,
Session 8 KiB, Display 4 KiB e MQTT 6 KiB. O tick é de 1 ms e o kernel executa
em um único núcleo.

Após cada ensaio, volte ao menu e salve o diagnóstico. Como critério inicial,
procure pelo menos 512 bytes de folga em cada pilha e 4 KiB de heap mínimo livre,
sem hooks de falha. Se não houver essa margem, registre a tarefa/ensaio e ajuste
a reserva antes de reduzir qualquer pilha. Avalie `late_ms`, `read_us`,
`skew_us`, `missed_slots` e `buffer_drops` do resumo de aquisição e a
distribuição dos intervalos do arquivo capturado.

A instrumentação e os testes automatizados estão implementados. A validação
final, a otimização baseada em medidas e a avaliação de SMP permanecem pendentes
dos ensaios físicos. O projeto segue configurado para um núcleo até existirem
medidas que justifiquem dividir a execução entre dois.

## Resultado automatizado desta revisão

- Firmware Pico W compilado com MQTT habilitado e desabilitado.
- 16 suítes de firmware no host aprovadas.
- Cinco suítes POSIX aprovadas, incluindo as cinco tarefas juntas.
- Validador de captura aprovado com dados de teste.
- Desktop: 18 testes aprovados e um teste de broker real ignorado.
- ELF com MQTT: `size` reportou 399140 bytes em text e 188352 bytes em bss.
  A seção bss inclui o heap reservado do FreeRTOS e os buffers estáticos.

Esses resultados registram a revisão de software. Preencha o roteiro de bancada
com o commit gravado, configuração, logs e medições da placa antes de declarar
a migração validada fisicamente.

## Revisão de memória após a migração

O objetivo desta revisão é reduzir reservas desnecessárias sem alterar a
arquitetura, a quantidade de mensagens pendentes ou a frequência de aquisição.
O firmware continua em um núcleo, com os mesmos tamanhos de pilha e heap.

Alterações:

- `include/telemetry.h`: cada posição passou de 768 para 704 bytes. A fila
  continua com 96 posições, economizando 6144 bytes; o buffer de envio MQTT
  acompanha a mesma constante e economiza outros 64 bytes.
- O dimensionamento inclui o terminador NUL. Mesmo duas linhas MPU usando
  todos os 255 caracteres de seus buffers, com device, sequência e timestamp
  nas maiores larguras, precisam de 672 bytes no total. Os demais formatos
  atuais de início, estado, tentativas e resultados são menores. Novos campos
  precisam respeitar esse orçamento; truncamento continua sendo contado como
  descarte, sem publicar JSON parcial.
- `src/monitoring.c`: removido o campo `sensor_t.timestamp_ms` e sua atualização,
  pois ninguém o consultava. O timestamp de cada amostra, em microssegundos,
  continua sendo registrado e transmitido normalmente.
- `src/display_task.c`: as chamadas de mutex, fila e notificação agora executam
  fora de `configASSERT`. Assim, uma eventual desativação das verificações não
  elimina a sincronização e a espera pela conclusão da transferência.

Comparação do ELF Release com MQTT, usando a mesma configuração:

| Seção (`arm-none-eabi-size`) | Antes | Depois | Redução |
| --- | ---: | ---: | ---: |
| text | 399140 | 399092 | 48 bytes |
| data | 0 | 0 | 0 |
| bss | 188352 | 182144 | 6208 bytes |

Esses números não são uma medição do pico de RAM em execução. Em particular,
o heap FreeRTOS de 40 KiB já está incluído em bss; não some novamente as pilhas
alocadas nesse heap. Os buffers de aquisição (14 KiB), o heap lwIP (16 KiB) e
o pool de pacotes foram preservados para não reduzir a tolerância a atrasos.
As funções antigas do driver OLED que não são referenciadas já são descartadas
pelo linker; removê-las do fonte não economizaria memória no binário atual.

Validação: builds com MQTT ligado e desligado, 16 suítes C no host, cinco suítes
POSIX FreeRTOS e `git diff --check` aprovados. O teste de telemetria cobre duas
linhas no limite do buffer, cópia para o consumidor, mensagem exatamente no
limite e rejeição de um byte excedente.

Para reproduzir, compile com `cmake --build build -j4` e execute
`python3 tests/run_host_tests.py` e `python3 tests/run_rtos_tests.py`.
Grave `build/tremo_device.uf2` pelo BOOTSEL e execute MOT, RTI, PRM e PAL com
MPU habilitado, incluindo pausa e reconexão MQTT. Ao voltar ao menu, registre
`RTOS_MEM` e `RTOS_STACK`. A aceitação na placa exige resultados completos,
ausência de falhas de heap/pilha e as margens descritas na Task 14. Perdas por
saturação devem aparecer nos contadores, sem descartes silenciosos.
Essa validação física continua pendente; não foram reduzidas pilhas com base
nos testes do computador.
