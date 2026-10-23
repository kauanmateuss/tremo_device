# Tremo Device — base de periféricos

Projeto em C com Pico SDK para a BitDogLab. A primeira tarefa implementada em `main.c` lê continuamente os dois MPUs e
mostra os eixos do acelerômetro e do giroscópio no monitor USB e no OLED.

## Organização

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

## Monitoramento contínuo

A lógica da aplicação está em `main.c`, com pequenas funções auxiliares no mesmo
arquivo para inicialização, aquisição, saída serial e atualização da tela.

- Leitura dos dois sensores: intervalo alvo de 40 ms (aproximadamente 25 Hz por sensor).
- OLED: atualização a cada 200 ms (aproximadamente 5 Hz).
- Serial USB: uma linha CSV por sensor em cada ciclo, enquanto conectado.
- Acelerômetro: faixa ±2 g. Giroscópio: faixa padrão após reset, ±250 graus/s.
- Falhas aparecem como `ERRO`. A aplicação tenta reinicializar o sensor a cada 2 s.
  Valores antigos não são exibidos como novas leituras.

Abra o monitor serial USB no Wokwi ou na placa. O cabeçalho é enviado ao detectar
a conexão, inclusive se o monitor for aberto depois da inicialização:

```text
tempo_ms,mpu,status,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps
```

O timestamp indica o término da tentativa de leitura de cada sensor, em
milissegundos desde o boot. Linhas de erro deixam os seis campos de medição vazios.
A UART foi desabilitada, pois seus GPIOs padrão 0 e 1 são usados pelo I2C0.

No OLED, cada sensor ocupa quatro linhas: cabeçalho `MPU1 MG DPS` (ou MPU2),
seguido de X, Y e Z. A primeira coluna numérica representa aceleração em mg
(milésimos de g), e a segunda, velocidade angular em graus/s.

Os dados ainda incluem gravidade, movimento voluntário e ruído. Esta etapa
visualiza o movimento medido; não calcula frequência, amplitude ou classificação
de tremor. Os sensores são lidos sequencialmente, sem sincronização.

O agendamento é simples e bloqueante: I2C, USB e reinicializações podem alongar o
intervalo entre amostras. Portanto, 25 Hz é uma meta, não uma frequência garantida.
Para a futura análise de tremores será necessário configurar a filtragem e a taxa
interna dos MPUs, calibrar e verificar a regularidade da aquisição.

## Contratos das funções

- `npInit` retorna `bool`; repetir com o mesmo pino não reserva outra máquina PIO.
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
```

Requer Pico SDK, CMake e compilador ARM configurados. O firmware é gerado em
`build/tremo_device.uf2`. A compilação não valida as ligações físicas.

Próximas tarefas simples:

1. Acender separadamente vermelho, verde e azul na matriz e conferir as cores.
2. Exibir oito linhas no OLED e conferir o corte de textos longos.
3. Variar aceleração e rotação de cada MPU no Wokwi e conferir serial e OLED.
4. Desconectar um sensor, verificar `ERRO` e testar sua recuperação.
5. Medir os intervalos pelos timestamps e acrescentar calibração e filtragem
   antes da análise de movimento.

Referências técnicas: [Pico SDK](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html)
e [registradores MPU6050](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf).
