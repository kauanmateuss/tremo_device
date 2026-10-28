# Tremo Device — base de periféricos

Projeto em C com Pico SDK para a BitDogLab. Uma FSM gerencia o menu e as sessões de monitoramento dos dois MPUs,
com saída no monitor USB e no OLED. Veja o [guia da task FSM](docs/fsm-monitoramento.md)
para arquitetura, transições, compilação e critérios de aceitação.
O menu cognitivo oferece o [MOT adaptado](docs/mot-adaptado.md) e o RTI adaptado,
com protocolos experimentais, treinamento e registros em RAM.

## Organização

- `main.c`: inicialização da aplicação e loop principal.
- `app.h` / `app.c`: FSM, telas, menu e controle das sessões.
- `controls.h` / `controls.c`: dois eixos do joystick, botões A/B, debounce e eventos.
- `mot.h` / `mot.c`: treinamento e avaliação MOT adaptada, resultados e exportação USB.
- `rti.h` / `rti.c`: RTI por soltura do botão A, temporização e resultados.
- `monitoring.h` / `monitoring.c`: preparação, aquisição, conversões e resultados.
- `matrix.h` / `matrix.c`: buffer e envio para os 25 LEDs no GPIO 7.
- `display.h` / `display.c`: interface simples de texto para o OLED 128×64.
- `ssd1306.h`, `ssd1306_i2c.h` / `ssd1306_i2c.c`: driver do OLED.
- `mpu6050_i2c.h` / `mpu6050_i2c.c`: acesso aos dois MPU6050.

Os módulos pressupõem chamadas sequenciais em um único núcleo.

## Ligações previstas pelo código

| Periférico | Barramento | SDA | SCL | Endereço |
|---|---|---|---|---|
| OLED | I2C1 | GPIO 14 | GPIO 15 | 0x3C |
| MPU 1 | I2C0 | GPIO 0 | GPIO 1 | 0x68 |
| MPU 2 | I2C0 | GPIO 0 | GPIO 1 | 0x69 |

Os dois MPUs compartilham SDA/SCL. Configure AD0 em nível baixo no primeiro e
alto no segundo para obter endereços diferentes. Confira a ligação física antes
de usar: a configuração de software não altera o endereço do sensor.

## Fluxo atual

Ao ligar: inicializar periféricos → verificar sensores → boas-vindas → menu.
O joystick seleciona, A confirma e B volta. As opções são tremores, cognitiva,
completa e configurações/informações. Cognitiva abre o submenu MOT/RTI/PRM/PAL/bateria. MOT e RTI estão implementados;
as outras opções e a avaliação completa mostram EM BREVE.

Para monitorar: instruções → A preparar → sensores OK → A iniciar → aquisição.
Uma sessão dura 30 segundos. B solicita interrupção; A confirma e B continua.
A coleta continua durante a confirmação. Uma falha de sensor encerra a sessão.

- Leitura com intervalo alvo de 40 ms; OLED atualizado a cada 200 ms.
- Serial USB: CSV de aceleração (g) e giroscópio (graus/s), com timestamp.
- OLED: eixos XYZ em mg e graus/s para cada sensor.
- Resultados: duração, contagens válidas/falhas e motivo de término, em RAM e serial.
- A nova sessão apaga o resumo anterior; desligar também. Não há armazenamento
  permanente nem cálculo de frequência/intensidade dos tremores nesta etapa.

A aplicação utiliza um único núcleo e agendamento cooperativo. I2C e OLED são
síncronos; a preparação dos dois MPUs leva aproximadamente 400 ms. Os intervalos
são metas, não garantias de tempo real. A UART permanece desabilitada porque os
GPIOs 0/1 estão reservados ao I2C0. A matriz é inicializada apagada.

## MOT adaptado

Cognitiva → MOT → instruções → três alvos de treino → A iniciar → dez alvos → resultados.
O joystick move o cursor azul em quatro direções até o alvo verde; A seleciona e
B encerra. Na sobreposição, o LED fica ciano. O prazo de cada alvo é de 10 s.
Treino exige três acertos e fica separado das métricas. Não há aquisição MPU durante
este fluxo. A média usa somente acertos avaliativos; acurácia inclui omissões como
não acertos. Os registros ficam em RAM até iniciar outro MOT ou desligar e são
exportados via USB ao finalizar/reconectar. Não são escores validados do CANTAB.
Consulte o guia do MOT para protocolo, limitações de temporização e testes na placa.

## RTI adaptado

Cognitiva → RTI → A treinar → três respostas válidas → A iniciar → dez tentativas.
Em cada tentativa, solte A e depois pressione e mantenha pressionado. Após uma
espera sorteada de 1–3 s, o LED central fica verde: solte A antes de 2 s.
Soltar antes do verde registra antecipação; atingir 2 s sem resposta válida registra
omissão. B encerra e mostra os resultados. O treino deve ser repetido se houver erro
e não entra na média. OLED apresenta contagens e média apenas das respostas válidas.

A FSM principal delega ao módulo `rti`; `controls` fornece a borda de soltura validada
por debounce de 30 ms, preservando seu timestamp inicial. Durante o trecho cronometrado
não há atualização do OLED, exportação serial nem aquisição MPU. A referência do
estímulo é registrada após enviar os LEDs; polling de 5 ms e latência física ainda
precisam de validação na placa. Este protocolo mede somente reação por soltura,
sem tempo de movimento ou escores equivalentes ao [RTI CANTAB](https://cambridgecognition.com/reaction-time-rti/).

Os registros `RTI,...` em CSV usam microssegundos, separam treino/teste e deixam
tempos inexistentes vazios. São enviados por USB ao finalizar/reconectar e ficam
em RAM até iniciar outro RTI ou desligar; a transmissão não tem confirmação de entrega.
Para aceitar a task na placa: gravar o UF2 via BOOTSEL, concluir treino e dez tentativas,
provocar antecipação e omissão, conferir contagens/CSV e testar B durante a espera
e após o verde. Os testes no PC cobrem esses fluxos e bordas temporais; não substituem
a medição física da temporização.

## Contratos das funções

- `npInit` retorna `bool`; repetir com o mesmo pino não reserva outra máquina PIO.
- `npSetPixel` usa coordenadas 0–4 a partir do canto superior esquerdo.
- `npSetLED` aceita índices de 0 a 24; índices inválidos são ignorados.
- `npClear` limpa o buffer. `npWrite` envia o buffer para a matriz.
- `display_draw_text` altera o buffer; `display_render` envia ao OLED.
- `clear_display` e `display_show_lines` já atualizam a tela.
- O texto usa caracteres de 8×8; `y` é alinhado para baixo a múltiplos de 8.
  São exibidas até oito linhas. Textos longos são cortados na borda direita.
  A fonte atual contém letras, números, sinal negativo e ponto decimal, sem acentos.
- `mpu6050_reset`, `mpu6050_set_accel_range` e `mpu6050_read_raw` retornam
  `false` em falha. As transferências I2C dos MPUs têm timeout de 10 ms por chamada.
- `mpu6050_get_accel_range` retorna 0–3, ou `MPU6050_RANGE_ERROR` em falha.
- A API opcional de bitmap retorna `bool` em `ssd1306_init_bm`; confira esse retorno.
  Inicialize cada estrutura uma vez e libere `ram_buffer` com `free` ao descartá-la.
  O bitmap deve conter pelo menos `width * height / 8` bytes.

O driver OLED ainda não propaga erros I2C para a aplicação. Sua inicialização
não confirma a presença física do display.

## Compilação e validação na placa

```sh
cmake -S . -B build
cmake --build build -j 4
python3 tests/run_host_tests.py
```

Requer Pico SDK, CMake e compilador ARM configurados. O firmware é gerado em
`build/tremo_device.uf2`. A compilação não valida as ligações físicas.

Próximas tarefas simples:

1. Acender separadamente vermelho, verde e azul na matriz e conferir as cores.
2. Exibir oito linhas no OLED e conferir o corte de textos longos.
3. Variar aceleração e rotação de cada MPU no Wokwi e conferir serial e OLED.
4. Testar as transições e falhas usando os critérios do guia FSM.
5. Medir os intervalos pelos timestamps e acrescentar calibração e filtragem
   antes da análise de movimento.

Referências técnicas: [Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html)
e [registradores MPU6050](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf).
