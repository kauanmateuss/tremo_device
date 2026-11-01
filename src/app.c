#include <stdio.h>
#include "app.h"
#include "controls.h"
#include "display.h"
#include "matrix.h"
#include "monitoring.h"
#include "mot.h"
#include "rti.h"
#include "pico/stdlib.h"

#define WELCOME_MS 2000
#define DISPLAY_MS 200

static app_state_t state;
static unsigned int menu_option, cognitive_option;
static uint32_t welcome_started_ms, session_started_ms, last_display_ms;
static bool matrix_ready;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void show_menu(void) {
    const char *options[] = {"1 TREMORES", "2 COGNITIVA", "3 COMPLETA", "4 CONFIG INFO"};
    char rows[4][16];
    const char *lines[] = {"MENU PRINCIPAL", "", rows[0], rows[1], rows[2], rows[3], "", "A OK  B VOLTAR"};
    for (unsigned int i = 0; i < 4; ++i)
        snprintf(rows[i], sizeof(rows[i]), "%c%s", menu_option == i ? '-' : ' ', options[i]);
    display_show_lines(lines, 8);
}

static void show_cognitive_menu(void) {
    const char *options[] = {"1 MOT", "2 RTI", "3 PRM", "4 PAL", "5 BATERIA"};
    char rows[5][16];
    const char *lines[] = {"AVAL COGNITIVA", "", rows[0], rows[1], rows[2], rows[3], rows[4], "A OK B VOLTAR"};
    for (unsigned int i = 0; i < 5; ++i)
        snprintf(rows[i], sizeof(rows[i]), "%c%s", cognitive_option == i ? '-' : ' ', options[i]);
    display_show_lines(lines, 8);
}

static void show_results(void) {
    const monitoring_result_t *result = monitoring_result();
    char duration[32], valid1[32], valid2[32], errors[32];
    snprintf(duration, sizeof(duration), "TEMPO %lu S", (unsigned long)(result->duration_ms / 1000));
    snprintf(valid1, sizeof(valid1), "M1 OK %lu", (unsigned long)result->valid[0]);
    snprintf(valid2, sizeof(valid2), "M2 OK %lu", (unsigned long)result->valid[1]);
    snprintf(errors, sizeof(errors), "ERROS %lu %lu", (unsigned long)result->errors[0], (unsigned long)result->errors[1]);
    const char *reason = result->reason == MONITORING_COMPLETE ? "CONCLUIDO" :
                         result->reason == MONITORING_INTERRUPTED ? "INTERROMPIDO" : "FALHA SENSOR";
    const char *lines[] = {"RESULTADOS", reason, duration, valid1, valid2, errors, "SALVO EM RAM", "A OU B MENU"};
    display_show_lines(lines, 8);
}

// A entrada em um estado desenha a tela uma vez. Só a aquisição atualiza periodicamente.
static void enter_state(app_state_t next) {
    state = next;
    switch (state) {
    case APP_WELCOME: {
        const char *lines[] = {"BEM-VINDO", "AO TREMO", "", "VERIFICACAO",
            monitoring_sensor_ready(0) ? "MPU1 OK" : "MPU1 FALHA",
            monitoring_sensor_ready(1) ? "MPU2 OK" : "MPU2 FALHA", "", "INICIANDO MENU"};
        display_show_lines(lines, 8);
        welcome_started_ms = now_ms();
        break;
    }
    case APP_MENU:
        show_menu();
        break;
    case APP_INSTRUCTIONS: {
        const char *lines[] = {"MONITORAMENTO", "AJUSTE SENSORES", "EM REPOUSO", "SESSAO 30 S", "", "B PEDE PARADA", "A PREPARAR", "B MENU"};
        display_show_lines(lines, 8);
        break;
    }
    case APP_PREPARING: {
        const char *lines[] = {"PREPARANDO", "VERIFICANDO", "OS DOIS MPUS"};
        display_show_lines(lines, 3);
        break;
    }
    case APP_READY: {
        const char *lines[] = {"SENSORES OK", "PRONTO", "", "A INICIAR", "B VOLTAR"};
        display_show_lines(lines, 5);
        break;
    }
    case APP_SENSOR_ERROR: {
        const char *lines[] = {"FALHA SENSOR", monitoring_sensor_ready(0) ? "MPU1 OK" : "MPU1 FALHA",
            monitoring_sensor_ready(1) ? "MPU2 OK" : "MPU2 FALHA", "VERIFIQUE FIOS", "", "A TENTAR", "B VOLTAR"};
        display_show_lines(lines, 7);
        break;
    }
    case APP_ACQUIRING:
        // A próxima atualização mostra a leitura; não reinicia a sessão ao cancelar parada.
        last_display_ms = now_ms() - DISPLAY_MS;
        break;
    case APP_STOP_CONFIRM: {
        const char *lines[] = {"ENCERRAR", "A SESSAO", "", "COLETA CONTINUA", "", "A ENCERRAR", "B CONTINUAR"};
        display_show_lines(lines, 7);
        break;
    }
    case APP_RESULTS:
        show_results();
        break;
    case APP_UNAVAILABLE: {
        const char *lines[] = {menu_option == 1 ? "AVAL COGNITIVA" : "AVAL COMPLETA", "", "EM BREVE", "", "B MENU"};
        display_show_lines(lines, 5);
        break;
    }
    case APP_COG_MENU:
        show_cognitive_menu();
        break;
    case APP_MOT:
        mot_begin();
        break;
    case APP_RTI:
        rti_begin();
        break;
    case APP_COG_UNAVAILABLE: {
        const char *names[] = {"MOT", "RTI", "PRM", "PAL", "BATERIA"};
        const char *lines[] = {names[cognitive_option], "EM BREVE", "", "B VOLTAR"};
        display_show_lines(lines, 4);
        break;
    }
    case APP_MOT_ERROR:
    case APP_RTI_ERROR: {
        const char *lines[] = {"MATRIZ FALHOU", state == APP_MOT_ERROR ? "MOT BLOQUEADO" : "RTI BLOQUEADO", "", "B VOLTAR"};
        display_show_lines(lines, 4);
        break;
    }
    case APP_INFO: {
        const char *lines[] = {"CONFIG E INFO", "TREMO V0.1", "SESSAO 30 S", "LEITURA 40 MS", "OLED 200 MS",
            matrix_ready ? "MATRIZ OK" : "MATRIZ FALHA", "CONFIG FIXA", "B MENU"};
        display_show_lines(lines, 8);
        break;
    }
    }
}

static void finish_session(monitoring_end_t reason) {
    monitoring_stop(now_ms(), reason);
    enter_state(APP_RESULTS);
}

void app_init(void) {
    stdio_init_all();
    controls_init();
    display_init();
    matrix_ready = npInit(LED_PIN); // Inicializa apagada.
    monitoring_init();
    monitoring_prepare(); // Ausência de sensor não impede acessar o menu.
    menu_option = cognitive_option = 0;
    enter_state(APP_WELCOME);
}

void app_update(void) {
    uint32_t now = now_ms();
    control_event_t event = controls_poll(now);
    // Captura o instante após aceitar o evento, antes de qualquer saída serial/OLED.
    uint64_t event_us = time_us_64();
    if (state == APP_MOT) {
        if (mot_update(event, event_us)) enter_state(APP_COG_MENU);
        mot_serial_update();
        return; // Nenhuma aquisição MPU nem saída do monitoramento durante o MOT.
    }
    if (state == APP_RTI) {
        if (rti_update(event, controls_confirm_state(), event_us)) enter_state(APP_COG_MENU);
        rti_serial_update();
        return; // Não executa MPU, MOT ou exportações de outros testes durante RTI.
    }
    monitoring_serial_update();
    if (state != APP_ACQUIRING && state != APP_STOP_CONFIRM) {
        mot_serial_update();
        rti_serial_update();
    }

    switch (state) {
    case APP_WELCOME:
        if (now - welcome_started_ms >= WELCOME_MS) enter_state(APP_MENU);
        break;
    case APP_MENU:
        if (event == CONTROL_UP) { menu_option = (menu_option + 3) % 4; show_menu(); }
        else if (event == CONTROL_DOWN) { menu_option = (menu_option + 1) % 4; show_menu(); }
        else if (event == CONTROL_CONFIRM) {
            enter_state(menu_option == 0 ? APP_INSTRUCTIONS :
                        menu_option == 1 ? APP_COG_MENU :
                        menu_option == 3 ? APP_INFO : APP_UNAVAILABLE);
        }
        break;
    case APP_INSTRUCTIONS:
        if (event == CONTROL_BACK) enter_state(APP_MENU);
        else if (event == CONTROL_CONFIRM) enter_state(APP_PREPARING);
        break;
    case APP_PREPARING:
        if (event == CONTROL_BACK) enter_state(APP_INSTRUCTIONS);
        else enter_state(monitoring_prepare() ? APP_READY : APP_SENSOR_ERROR);
        break;
    case APP_READY:
        if (event == CONTROL_BACK) enter_state(APP_INSTRUCTIONS);
        else if (event == CONTROL_CONFIRM) {
            session_started_ms = now_ms();
            monitoring_start(session_started_ms);
            enter_state(APP_ACQUIRING);
        }
        break;
    case APP_SENSOR_ERROR:
        if (event == CONTROL_BACK) enter_state(APP_INSTRUCTIONS);
        else if (event == CONTROL_CONFIRM) enter_state(APP_PREPARING);
        break;
    case APP_ACQUIRING:
    case APP_STOP_CONFIRM:
        if (state == APP_STOP_CONFIRM && event == CONTROL_CONFIRM) {
            finish_session(MONITORING_INTERRUPTED);
            break;
        }
        if (now - session_started_ms >= MONITORING_DURATION_MS) {
            finish_session(MONITORING_COMPLETE);
            break;
        }
        if (event == CONTROL_BACK)
            enter_state(state == APP_ACQUIRING ? APP_STOP_CONFIRM : APP_ACQUIRING);
        if (!monitoring_update(now)) { finish_session(MONITORING_SENSOR_ERROR); break; }
        if (state == APP_ACQUIRING && now - last_display_ms >= DISPLAY_MS) {
            monitoring_draw();
            last_display_ms = now;
        }
        break;
    case APP_RESULTS:
        if (event == CONTROL_BACK || event == CONTROL_CONFIRM) enter_state(APP_MENU);
        break;
    case APP_COG_MENU:
        if (event == CONTROL_BACK) enter_state(APP_MENU);
        else if (event == CONTROL_UP) { cognitive_option = (cognitive_option + 4) % 5; show_cognitive_menu(); }
        else if (event == CONTROL_DOWN) { cognitive_option = (cognitive_option + 1) % 5; show_cognitive_menu(); }
        else if (event == CONTROL_CONFIRM) {
            if (cognitive_option == 0) enter_state(matrix_ready ? APP_MOT : APP_MOT_ERROR);
            else if (cognitive_option == 1) enter_state(matrix_ready ? APP_RTI : APP_RTI_ERROR);
            else enter_state(APP_COG_UNAVAILABLE);
        }
        break;
    case APP_COG_UNAVAILABLE:
    case APP_MOT_ERROR:
    case APP_RTI_ERROR:
        if (event == CONTROL_BACK) enter_state(APP_COG_MENU);
        break;
    case APP_MOT:
    case APP_RTI:
        break; // Tratado antes deste switch para isolar a execução cronometrada.
    case APP_UNAVAILABLE:
    case APP_INFO:
        if (event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    }
}

app_state_t app_get_state(void) {
    return state;
}
