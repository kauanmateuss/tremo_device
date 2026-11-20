# Tremo Device

O Tremo é um protótipo para registrar movimentos com dois sensores inerciais e executar avaliações cognitivas adaptadas na BitDogLab (Raspberry Pi Pico W). A aplicação desktop recebe os dados por Wi-Fi/MQTT e permite associar pacientes à placa, consultar resultados e comparar sessões.

Para experimentar a interface com dados simulados, siga o [guia do desktop](desktop/README.md).

## Compilar e gravar o firmware

Na raiz do repositório, use o Pico SDK 2.3.1 com a toolchain ARM, CMake, Ninja e o FreeRTOS Kernel. A compilação procura o kernel em `../FreeRTOS-Kernel`. Para preparar essa dependência pela primeira vez:

```sh
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git ../FreeRTOS-Kernel
git -C ../FreeRTOS-Kernel checkout ca759444e
```

Para enviar dados ao desktop, copie o exemplo de configuração na primeira instalação. Preencha `config/network_config.h` com os dados da rede Wi-Fi de 2,4 GHz e do broker MQTT acessível pela placa.

```sh
cp -n config/network_config.example.h config/network_config.h
```

Com o ambiente do Pico SDK configurado, compile para a Pico W:

```sh
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w -DTREMO_MQTT=ON
cmake --build build -j 4
```

1. Segure **BOOTSEL** ao conectar a placa por USB. Solte o botão e copie `build/tremo_device.uf2` para a unidade **RPI-RP2**.
2. Para registrar dados de um paciente, abra o [desktop](desktop/README.md), conecte ao broker e associe o paciente à placa.
3. No OLED, abra **Sessões** e pressione **A** para iniciar. Clique no joystick para voltar ao menu. Escolha **Aval individual** para um teste ou **Aval completa** para a sequência. Os ajustes ficam em **Info da placa**.

Consulte os guias de [sensores e avaliações](docs/avaliacoes.md), [comunicação MQTT](docs/mqtt.md) e [validação em bancada](docs/validacao-tecnica.md).
