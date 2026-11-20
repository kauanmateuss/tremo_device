# Tremo Desktop

O dashboard permite associar pacientes à BitDogLab, acompanhar os dois sensores e consultar resultados de avaliações por sessão. A aplicação recebe dados por MQTT e mantém o histórico em SQLite.

## Instalar

Na raiz do repositório, com Python 3.11 ou superior:

```sh
python3 -m venv desktop/.venv
source desktop/.venv/bin/activate
python -m pip install -e 'desktop[gui]'
```

## Experimentar sem placa

```sh
python -m tremo_desktop gui --demo
```

A demonstração abre sessões fictícias em um banco temporário.

## Usar com a placa

1. Inicie um broker MQTT, como o Mosquitto, acessível pelo computador e pela placa. Grave o [firmware configurado para esse broker](../README.md).
2. Com o ambiente virtual ativo, abra o dashboard com o banco de trabalho:

   ```sh
   python -m tremo_desktop --db desktop/tremo.sqlite3 gui
   ```

3. Em **Configurações**, informe o endereço, a porta e as credenciais do broker, quando exigidas. Clique em **Aplicar conexão**. A própria janela inicia o coletor.
4. Selecione a placa, associe o paciente e aguarde a confirmação. Inicie uma sessão no OLED. Os resultados ficam disponíveis em **Histórico**.

Consulte o [guia de resultados e coleta por terminal](../docs/dashboard.md).
