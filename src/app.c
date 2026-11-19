#include <stdio.h>
#include "app.h"
#include "controls.h"
#include "display.h"
#include "matrix.h"
#include "monitoring.h"
#include "mot.h"
#include "rti.h"
#include "prm.h"
#include "pal.h"
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "session.h"
#include "assessment.h"
#include "communication.h"
#include "evaluation.h"
#include "patient_assoc.h"
#include "telemetry.h"

#define WELCOME_MS 2000
#define DISPLAY_MS 200

static app_state_t state;
static unsigned int menu_option, cognitive_option, config_option;
static uint32_t welcome_started_ms, last_display_ms;
static bool matrix_ready;
static int preparing_test = -1;
static bool capturing_cognitive, manual_session, counted_test, full_evaluation;
static unsigned test_count;
static bool begin_session(session_kind_t kind) {
    char patient[PATIENT_ID_MAX + 1];
    patient_assoc_copy(patient);
    return session_begin_patient(kind, time_us_64(), patient);
}
static void finish_automatic(session_state_t reason) {
    if (!manual_session) session_finish(reason, time_us_64());
    else if (session_current()->state == SESSION_PAUSED) session_resume(time_us_64());
}
static const app_state_t cognitive_states[] = {APP_MOT, APP_RTI, APP_PRM, APP_PAL};
static communication_status_t ready_network;
static char device_id_str[17];

static void show_device_menu(void);
static void show_device_info(void);
static void show_sessions(void);
static void show_session_active(void);

static void draw_ready(void) {
    static const char *const labels[] = {
        [COMM_NO_RADIO] = "SEM RADIO", [COMM_WIFI_WAIT] = "BUSCANDO WIFI",
        [COMM_WIFI_NOT_FOUND] = "REDE AUSENTE", [COMM_WIFI_BAD_AUTH] = "VER SENHA WIFI",
        [COMM_WIFI_NO_IP] = "WIFI SEM IP", [COMM_BROKER_WAIT] = "AGUARDE MQTT",
        [COMM_ONLINE] = "MQTT CONECTADO"
    };
    const char *titles[] = {"MOT + MPU", "RTI + MPU", "PRM + MPU", "PAL + MPU"};
    ready_network = communication_status();
    const char *lines[] = {preparing_test >= 0 ? titles[preparing_test] : "SENSORES OK", "PRONTO", labels[ready_network],
                           "", "A INICIAR", "B VOLTAR"};
    display_show_lines(lines, 6);
}

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void show_menu(void) {
    show_device_menu();
}

static void show_cognitive_menu(void) {
    const char *options[] = {"MOT", "RTI", "PRM", "PAL", "BATERIA", "TREMORES"};
    char rows[6][16];
    const char *lines[] = {"AVAL INDIVIDUAL", rows[0], rows[1], rows[2], rows[3], rows[4], rows[5], "A OK B VOLTAR"};
    for (unsigned int i = 0; i < 6; ++i)
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

static void show_config(void) {
    char rows[8][24];
    const session_config_t *c = session_config();
    snprintf(rows[0], sizeof(rows[0]), "TEMPO %lu S", (unsigned long)(c->duration_ms / 1000));
    snprintf(rows[1], sizeof(rows[1]), "TAXA %u HZ", 1000 / c->sample_ms);
    const char *names[] = {"MOT", "RTI", "PRM", "PAL"};
    for (unsigned i = 0; i < 4; ++i) snprintf(rows[i + 2], sizeof(rows[0]), "%s %s", names[i], c->tests_mask & (1u << i) ? "SIM" : "NAO");
    snprintf(rows[6], sizeof(rows[6]), "MPU %s", c->tremor_first ? "ANTES" : "DEPOIS");
    snprintf(rows[7], sizeof(rows[7]), "MPU COG %s", c->cognitive_mpu ? "SIM" : "NAO");
    // Janela de três opções mantém instruções visíveis no OLED.
    const char *lines[] = {"CONFIGURACOES", rows[config_option], rows[(config_option + 1) % 8], rows[(config_option + 2) % 8], "ALTERA PRIMEIRA", "A ALTERAR", "B MENU", "CLIQUE PAUSA"};
    display_show_lines(lines, 8);
}
static void show_device_menu(void) {
    const char *options[] = {"AVAL COMPLETA", "AVAL INDIVIDUAL", "SESSOES", "INFO DA PLACA"};
    char rows[4][24];
    const char *lines[] = {"MENU PRINCIPAL", "", rows[0], rows[1], rows[2], rows[3], "", "A OK  B VOLTAR"};
    for (unsigned int i = 0; i < 4; ++i)
        snprintf(rows[i], sizeof(rows[i]), "%c%s", menu_option == i ? '-' : ' ', options[i]);
    display_show_lines(lines, 8);
}

static void show_device_info(void) {
    char rows[8][24];
    snprintf(rows[0], sizeof(rows[0]), "%s", device_id_str);
    if (app_has_patient()) {
        snprintf(rows[1], sizeof(rows[1]), "%s", app_patient_id());
    } else {
        snprintf(rows[1], sizeof(rows[1]), "SEM PACIENTE");
    }
    communication_status_t net = communication_status();
    const char *net_str;
    switch (net) {
    case COMM_ONLINE: net_str = "MQTT ONLINE"; break;
    case COMM_BROKER_WAIT: net_str = "MQTT AGUARDANDO"; break;
    case COMM_WIFI_WAIT: net_str = "WIFI CONECTANDO"; break;
    case COMM_WIFI_NOT_FOUND: net_str = "REDE AUSENTE"; break;
    case COMM_WIFI_BAD_AUTH: net_str = "VER SENHA WIFI"; break;
    case COMM_WIFI_NO_IP: net_str = "WIFI SEM IP"; break;
    default: net_str = "SEM RADIO"; break;
    }
    snprintf(rows[2], sizeof(rows[2]), "%s", net_str);
    snprintf(rows[3], sizeof(rows[3]), "MPU1: %s", monitoring_sensor_ready(0) ? "OK" : "FALHA");
    snprintf(rows[4], sizeof(rows[4]), "MPU2: %s", monitoring_sensor_ready(1) ? "OK" : "FALHA");
    const char *lines[] = {"INFO DISPOSITIVO", rows[0], rows[1], rows[2], rows[3], rows[4], "A CONFIGURAR", "B VOLTAR"};
    display_show_lines(lines, 8);
}

static void show_sessions(void) {
    const char *lines[] = {"SESSOES", "SEM SESSAO ATIVA", "", "A INICIAR", "JOY VOLTAR"};
    display_show_lines(lines, 5);
}

static void show_session_active(void) {
    const session_t *s = session_current();
    uint64_t active = session_active_us(time_us_64());
    unsigned minutes = (unsigned)(active / 60000000ULL);
    unsigned seconds = (unsigned)((active % 60000000ULL) / 1000000ULL);
    char id_str[24];
    snprintf(id_str, sizeof(id_str), "%016llx", (unsigned long long)s->id);
    const char *patient = s->patient_id[0] ? s->patient_id : "SEM PACIENTE";
    const char *state_str = s->state == SESSION_RUNNING ? "ATIVA" : s->state == SESSION_PAUSED ? "PAUSADA" : "ENCERRADA";
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02u:%02u", minutes, seconds);
    char tests[24];
    snprintf(tests, sizeof(tests), "TESTES %u", test_count);
    const char *lines[] = {"SESSAO ATIVA", id_str, patient, state_str, time_str, tests, "B ENCERRAR", "JOY VOLTAR"};
    display_show_lines(lines, 8);
}

static void show_evaluation_results(void) {
    const evaluation_t *e = evaluation_current();
    const char *names[] = {"MOT", "RTI", "PRM", "PAL", "MPU"};
    const char *status[] = {"PENDENTE", "EXECUTANDO", "OK", "LIMITE", "PARADO", "FALHA"};
    char rows[5][24];
    for (unsigned i = 0; i < 5; ++i) {
        if (i < e->count) snprintf(rows[i], sizeof(rows[i]), "%s %s", names[e->steps[i].module], status[e->steps[i].status]);
        else rows[i][0] = 0;
    }
    const char *lines[] = {"RESUMO AVALIACAO", manual_session ? "SESSAO ABERTA" : session_current()->state == SESSION_COMPLETE ? "CONCLUIDA" : "ENCERRADA",
        rows[0], rows[1], rows[2], rows[3], rows[4], "A OU B MENU"};
    display_show_lines(lines, 8);
    if (stdio_usb_connected()) {
        printf("# EVALUATION id=%016llx state=%u active_us=%llu pauses=%lu\n", (unsigned long long)e->session_id,
            session_current()->state, (unsigned long long)session_active_us(time_us_64()), (unsigned long)session_current()->pauses);
        for (unsigned i = 0; i < e->count; ++i) printf("# STEP module=%u status=%u correct=%lu wrong=%lu omissions=%lu\n",
            e->steps[i].module, e->steps[i].status, (unsigned long)e->steps[i].correct,
            (unsigned long)e->steps[i].wrong, (unsigned long)e->steps[i].omissions);
    }
}

static void begin_cognitive(unsigned test) {
    const session_kind_t kinds[] = {SESSION_MOT, SESSION_RTI, SESSION_PRM, SESSION_PAL};
    if (!evaluation_current()->active && !manual_session) begin_session(kinds[test]);
    telemetry_next_run();
    counted_test = false;
    static const char *const modules[] = {"mot", "rti", "prm", "pal"};
    telemetry_emit(session_current()->id, time_us_64(), "test_start", true,
                   "{\"module\":\"%s\"}", modules[test]);
    assessment_begin(test);
    if (session_current()->config.cognitive_mpu) {
        monitoring_start_continuous(now_ms());
        capturing_cognitive = true;
    }
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
        const char *lines[] = {"MONITORAMENTO", "AJUSTE SENSORES", "EM REPOUSO", "TEMPO EM CONFIG", "", "B PEDE PARADA", "A PREPARAR", "B MENU"};
        display_show_lines(lines, 8);
        break;
    }
    case APP_PREPARING: {
        const char *lines[] = {"PREPARANDO", "VERIFICANDO", "OS DOIS MPUS"};
        display_show_lines(lines, 3);
        break;
    }
    case APP_CALIBRATING: {
        const char *lines[] = {"CALIBRANDO", "APOIE OS MPUS", "MANTENHA IMOVEL", "", "B CANCELAR"};
        display_show_lines(lines, 5); break;
    }
    case APP_CAL_ERROR: {
        const char *lines[] = {"CALIBRACAO", "MOVIMENTO FALHA", "APOIE OS MPUS", "", "A REPETIR", "B VOLTAR"};
        display_show_lines(lines, 6); break;
    }
    case APP_READY:
        draw_ready();
        break;
    case APP_SENSOR_ERROR: {
        const char *lines[] = {"FALHA SENSOR", monitoring_sensor_ready(0) ? "MPU1 OK" : "MPU1 FALHA",
            monitoring_sensor_ready(1) ? "MPU2 OK" : "MPU2 FALHA", "VER SERIAL USB", "", "A TENTAR", "B VOLTAR"};
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
    case APP_MOT: begin_cognitive(0); break;
    case APP_RTI: begin_cognitive(1); break;
    case APP_PRM: begin_cognitive(2); break;
    case APP_PAL: begin_cognitive(3); break;
    case APP_COG_UNAVAILABLE: {
        const char *names[] = {"MOT", "RTI", "PRM", "PAL", "BATERIA"};
        const char *lines[] = {names[cognitive_option], "EM BREVE", "", "B VOLTAR"};
        display_show_lines(lines, 4);
        break;
    }
    case APP_MOT_ERROR:
    case APP_RTI_ERROR:
    case APP_PRM_ERROR:
    case APP_PAL_ERROR: {
        const char *lines[] = {"MATRIZ FALHOU", state == APP_MOT_ERROR ? "MOT BLOQUEADO" : state == APP_RTI_ERROR ? "RTI BLOQUEADO" : state == APP_PRM_ERROR ? "PRM BLOQUEADO" : "PAL BLOQUEADO", "", "B VOLTAR"};
        display_show_lines(lines, 4);
        break;
    }
    case APP_EVAL_SETUP: {
        const char *lines[] = {full_evaluation ? "AVAL COMPLETA" : "BATERIA", "VEJA CONFIG", "UMA POR VEZ", "A PROX B FIM", "CLIQUE PAUSA", "", "A INICIAR", "B CANCELAR"};
        display_show_lines(lines, 8); break;
    }
    case APP_EVAL_RESULTS:
        show_evaluation_results(); break;
    case APP_PAUSED: {
        const char *lines[] = {"SESSAO PAUSADA", "", "A CONTINUAR", "B ENCERRAR"};
        display_show_lines(lines, 4); break;
    }
    case APP_INFO:
        show_config(); break;
    case APP_DEVICE_INFO:
        show_device_info(); break;
    case APP_SESSION_CLOSE: {
        const char *lines[] = {"ENCERRAR SESSAO?", "", "A CONFIRMAR", "B CANCELAR"};
        display_show_lines(lines, 4);
        break;
    }
    case APP_SESSIONS:
        show_sessions(); break;
    case APP_SESSION_ACTIVE:
        show_session_active(); break;
    }
}

static void evaluation_end(session_state_t reason);
static void finish_session(monitoring_end_t reason) {
    monitoring_stop(now_ms(), reason);
    reason = monitoring_result()->reason;
    ++test_count;
    if (!evaluation_current()->active) finish_automatic(reason == MONITORING_COMPLETE ? SESSION_COMPLETE : reason == MONITORING_INTERRUPTED ? SESSION_INTERRUPTED : SESSION_ERROR);
    if (evaluation_current()->active) {
        evaluation_record(reason == MONITORING_COMPLETE ? STEP_COMPLETE : reason == MONITORING_INTERRUPTED ? STEP_INTERRUPTED : STEP_ERROR, 0, 0, 0);
        if (reason != MONITORING_COMPLETE) { evaluation_end(reason == MONITORING_INTERRUPTED ? SESSION_INTERRUPTED : SESSION_ERROR); return; }
    }
    enter_state(APP_RESULTS);
    if (evaluation_current()->active) { display_draw_text("A PROX B FIM   ", 5, 56); display_render(); }
}

static void evaluation_end(session_state_t reason) {
    preparing_test = -1;
    if (reason != SESSION_COMPLETE) evaluation_cancel(reason == SESSION_ERROR);
    finish_automatic(reason);
    enter_state(APP_EVAL_RESULTS);
}
static void launch_cognitive(unsigned test) {
    if (session_config()->cognitive_mpu) {
        preparing_test = (int)test;
        enter_state(APP_PREPARING);
    } else enter_state(cognitive_states[test]);
}

static void leave_preparation(void) {
    if (preparing_test >= 0) {
        preparing_test = -1;
        if (evaluation_current()->active) evaluation_end(SESSION_INTERRUPTED);
        else enter_state(APP_COG_MENU);
    } else enter_state(APP_INSTRUCTIONS);
}

static void launch_step(void) {
    evaluation_start_step();
    unsigned module = evaluation_current()->steps[evaluation_current()->index].module;
    if (module == EVALUATION_TREMOR) enter_state(APP_INSTRUCTIONS);
    else launch_cognitive(module);
}
static void launch_evaluation(bool full) {
    if (!matrix_ready) { enter_state(APP_MOT_ERROR); return; }
    if (!manual_session && !begin_session(full ? SESSION_FULL : SESSION_BATTERY)) return;
    if (evaluation_begin(full, session_current())) {
        full_evaluation = full;
        enter_state(APP_EVAL_SETUP);
    }
}
static void evaluation_continue(control_event_t event) {
    if (event == CONTROL_BACK) evaluation_end(SESSION_INTERRUPTED);
    else if (event == CONTROL_CONFIRM) {
        if (evaluation_next()) launch_step();
        else evaluation_end(SESSION_COMPLETE);
    }
}
static void record_cognitive_step(bool sensor_error) {
    unsigned module = evaluation_current()->steps[evaluation_current()->index].module;
    switch (module) {
    case 0: evaluation_record(sensor_error ? STEP_ERROR : assessment_interrupted() ? STEP_INTERRUPTED : STEP_COMPLETE, mot_result()->correct, mot_result()->wrong, mot_result()->omissions); break;
    case 1: evaluation_record(sensor_error ? STEP_ERROR : assessment_interrupted() ? STEP_INTERRUPTED : STEP_COMPLETE, rti_result()->responses, rti_result()->anticipations, rti_result()->omissions); break;
    case 2: evaluation_record(sensor_error ? STEP_ERROR : assessment_interrupted() ? STEP_INTERRUPTED : STEP_COMPLETE, prm_result()->correct, prm_result()->wrong, prm_result()->omissions); break;
    case 3: evaluation_record(sensor_error ? STEP_ERROR : assessment_interrupted() ? STEP_INTERRUPTED : pal_result()->limit_reached ? STEP_LIMIT : STEP_COMPLETE, pal_result()->correct, pal_result()->wrong, pal_result()->omissions); break;
    }
    if (sensor_error) evaluation_end(SESSION_ERROR);
    else if (assessment_interrupted()) evaluation_end(SESSION_INTERRUPTED);
    else { display_draw_text("A PROX B FIM   ", 5, 56); display_render(); }
}

const char *app_device_id(void) {
    return patient_assoc_device_id();
}

const char *app_patient_id(void) {
    static char patient[PATIENT_ID_MAX + 1];
    patient_assoc_copy(patient);
    return patient;
}

bool app_has_patient(void) {
    return app_patient_id()[0] != 0;
}

void app_init(void) {
    evaluation_cancel(false);
    communication_init();
    manual_session = false;
    session_init(get_rand_32());
    controls_init();
    display_init();
    matrix_ready = npInit(LED_PIN); // Inicializa apagada.
    monitoring_init();
    monitoring_prepare(); // Ausência de sensor não impede acessar o menu.
    menu_option = cognitive_option = config_option = 0;
    preparing_test = -1;
    capturing_cognitive = false;
    // Copia o ID do dispositivo para exibicao
    snprintf(device_id_str, sizeof(device_id_str), "%s", patient_assoc_device_id());
    enter_state(APP_WELCOME);
}

static void report_cognitive_sensor_error(void) {
    if (!counted_test) { ++test_count; counted_test = true; }
    if (evaluation_current()->active) {
        record_cognitive_step(true);
    } else {
        finish_automatic(SESSION_ERROR);
        const char *lines[] = {"FALHA MPU", "TESTE ENCERRADO", "", "VER SERIAL USB", "A OU B VOLTAR"};
        display_show_lines(lines, 5);
    }
}

static void app_step(control_event_t event, control_button_state_t button, uint64_t event_us, uint64_t processing_us) {
    uint32_t now = now_ms();
    if (state == APP_MOT || state == APP_RTI || state == APP_PRM || state == APP_PAL) {
        if (session_current()->config.cognitive_mpu)
            monitoring_serial_update();
        if (capturing_cognitive) {
            if (!monitoring_update(now_ms())) {
                assessment_update(CONTROL_BACK, button, event_us, processing_us);
                monitoring_stop(now_ms(), MONITORING_SENSOR_ERROR);
                capturing_cognitive = false;
                report_cognitive_sensor_error();
                return;
            }
        }
        if (evaluation_current()->active && assessment_finished()) { assessment_update(CONTROL_NONE, button, event_us, processing_us); evaluation_continue(event); return; }
        bool leave = assessment_update(event, button, event_us, processing_us);
        if (assessment_finished() && !counted_test) {
            counted_test = true;
            ++test_count;
            if (capturing_cognitive) {
                monitoring_stop(now_ms(), assessment_interrupted() ? MONITORING_INTERRUPTED : MONITORING_COMPLETE);
                capturing_cognitive = false;
                if (monitoring_result()->reason == MONITORING_SENSOR_ERROR) {
                    report_cognitive_sensor_error();
                    return;
                }
            }
            if (evaluation_current()->active) record_cognitive_step(false);
            else finish_automatic(assessment_interrupted() ? SESSION_INTERRUPTED : SESSION_COMPLETE);
        }
        if (leave) enter_state(APP_COG_MENU);
        return;
    }
    monitoring_serial_update();
    if (state != APP_ACQUIRING && state != APP_STOP_CONFIRM) {
        mot_serial_update();
        rti_serial_update();
        prm_serial_update();
        pal_serial_update();
    }

    now = now_ms(); // A saída USB pode consumir tempo antes de agendar a leitura.
    switch (state) {
    case APP_WELCOME:
        if (now - welcome_started_ms >= WELCOME_MS) enter_state(APP_MENU);
        break;
    case APP_MENU:
        if (event == CONTROL_UP) { menu_option = (menu_option + 3) % 4; show_menu(); }
        else if (event == CONTROL_DOWN) { menu_option = (menu_option + 1) % 4; show_menu(); }
        else if (event == CONTROL_CONFIRM) {
            if (menu_option == 0) launch_evaluation(true);
            else if (menu_option == 1) enter_state(APP_COG_MENU);
            else if (menu_option == 2) enter_state(manual_session ? APP_SESSION_ACTIVE : APP_SESSIONS);
            else if (menu_option == 3) enter_state(APP_DEVICE_INFO);
        }
        break;
    case APP_INSTRUCTIONS:
        if (event == CONTROL_BACK) { if (evaluation_current()->active) evaluation_end(SESSION_INTERRUPTED); else enter_state(APP_MENU); }
        else if (event == CONTROL_CONFIRM) enter_state(APP_PREPARING);
        break;
    case APP_PREPARING:
        if (event == CONTROL_BACK) leave_preparation();
        else if (monitoring_prepare()) {
            monitoring_calibration_begin(now_ms()); enter_state(APP_CALIBRATING);
        } else if (evaluation_current()->active) evaluation_end(SESSION_ERROR);
        else enter_state(APP_SENSOR_ERROR);
        break;
    case APP_CALIBRATING:
        if (event == CONTROL_BACK) { monitoring_calibration_cancel(); leave_preparation(); }
        else {
            calibration_state_t c = monitoring_calibration_update(now);
            if (c == CAL_OK) enter_state(APP_READY);
            else if (c == CAL_MOVING) enter_state(APP_CAL_ERROR);
            else if (c == CAL_SENSOR_ERROR) { if (evaluation_current()->active) evaluation_end(SESSION_ERROR); else enter_state(APP_SENSOR_ERROR); }
        }
        break;
    case APP_CAL_ERROR:
        if (event == CONTROL_CONFIRM) { monitoring_calibration_begin(now); enter_state(APP_CALIBRATING); }
        else if (event == CONTROL_BACK) leave_preparation();
        break;
    case APP_READY:
        if (ready_network != communication_status()) draw_ready();
        if (event == CONTROL_BACK) leave_preparation();
        else if (event == CONTROL_CONFIRM && preparing_test >= 0) {
            unsigned test = (unsigned)preparing_test;
            preparing_test = -1;
            enter_state(cognitive_states[test]);
        } else if (event == CONTROL_CONFIRM) {
            if (!evaluation_current()->active && !manual_session) begin_session(SESSION_TREMOR);
            telemetry_next_run();
            monitoring_start(now_ms());
            enter_state(APP_ACQUIRING);
        }
        break;
    case APP_SENSOR_ERROR:
        if (event == CONTROL_BACK) leave_preparation();
        else if (event == CONTROL_CONFIRM) enter_state(APP_PREPARING);
        break;
    case APP_ACQUIRING:
    case APP_STOP_CONFIRM:
        if (state == APP_STOP_CONFIRM && event == CONTROL_CONFIRM) {
            finish_session(MONITORING_INTERRUPTED);
            break;
        }
        if (monitoring_elapsed_ms(now) >= session_current()->config.duration_ms) {
            finish_session(MONITORING_COMPLETE);
            break;
        }
        if (event == CONTROL_PAUSE || session_current()->pause_requested) {
            monitoring_pause(now); session_pause(time_us_64()); enter_state(APP_PAUSED); break;
        }
        if (event == CONTROL_BACK)
            enter_state(state == APP_ACQUIRING ? APP_STOP_CONFIRM : APP_ACQUIRING);
        if (!monitoring_update(now)) { finish_session(MONITORING_SENSOR_ERROR); break; }
        if (state == APP_ACQUIRING && now - last_display_ms >= DISPLAY_MS) {
            monitoring_draw();
            last_display_ms = now;
        }
        break;
    case APP_PAUSED:
        if (event == CONTROL_BACK) finish_session(MONITORING_INTERRUPTED);
        else if (event == CONTROL_CONFIRM) {
            session_resume(time_us_64()); monitoring_resume(now); enter_state(APP_ACQUIRING);
        }
        break;
    case APP_EVAL_SETUP:
        if (event == CONTROL_BACK) evaluation_end(SESSION_INTERRUPTED);
        else if (event == CONTROL_CONFIRM) launch_step();
        break;
    case APP_EVAL_RESULTS:
        if (event == CONTROL_CONFIRM || event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    case APP_RESULTS:
        if (evaluation_current()->active) { evaluation_continue(event); break; }
        if (event == CONTROL_BACK || event == CONTROL_CONFIRM) enter_state(APP_MENU);
        break;
    case APP_COG_MENU:
        if (event == CONTROL_BACK) enter_state(APP_MENU);
        else if (event == CONTROL_UP) { cognitive_option = (cognitive_option + 5) % 6; show_cognitive_menu(); }
        else if (event == CONTROL_DOWN) { cognitive_option = (cognitive_option + 1) % 6; show_cognitive_menu(); }
        else if (event == CONTROL_CONFIRM) {
            if (cognitive_option < 4) {
                const app_state_t errors[] = {APP_MOT_ERROR, APP_RTI_ERROR, APP_PRM_ERROR, APP_PAL_ERROR};
                if (matrix_ready) launch_cognitive(cognitive_option);
                else enter_state(errors[cognitive_option]);
            }
            else if (cognitive_option == 4) launch_evaluation(false);
            else enter_state(APP_INSTRUCTIONS);
        }
        break;
    case APP_COG_UNAVAILABLE:
    case APP_MOT_ERROR:
    case APP_RTI_ERROR:
    case APP_PRM_ERROR:
    case APP_PAL_ERROR:
        if (event == CONTROL_BACK) enter_state(APP_COG_MENU);
        break;
    case APP_MOT:
    case APP_RTI:
    case APP_PRM:
    case APP_PAL:
        break; // Tratado antes deste switch para isolar a execução cronometrada.
    case APP_INFO:
        if (event == CONTROL_UP || event == CONTROL_DOWN) { config_option = (config_option + (event == CONTROL_DOWN ? 1 : 7)) % 8; show_config(); }
        else if (event == CONTROL_CONFIRM) {
            session_config_t c = *session_config();
            if (config_option == 0) c.duration_ms = c.duration_ms == 30000 ? 60000 : 30000;
            else if (config_option == 1) c.sample_ms = c.sample_ms == 40 ? 20 : 40;
            else if (config_option < 6) c.tests_mask ^= 1u << (config_option - 2);
            else if (config_option == 6) c.tremor_first = !c.tremor_first;
            else c.cognitive_mpu = !c.cognitive_mpu;
            session_configure(c); show_config();
        } else if (event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    case APP_DEVICE_INFO:
        if (event == CONTROL_NONE && now - last_display_ms >= 1000) {
            last_display_ms = now;
            show_device_info();
        } else if (event == CONTROL_CONFIRM) enter_state(APP_INFO);
        else if (event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    case APP_SESSIONS:
        if (event == CONTROL_CONFIRM && begin_session(SESSION_MANUAL)) {
            manual_session = true;
            test_count = 0;
            enter_state(APP_SESSION_ACTIVE);
        } else if (event == CONTROL_PAUSE || event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    case APP_SESSION_ACTIVE:
        if (event == CONTROL_BACK) enter_state(APP_SESSION_CLOSE);
        else if (event == CONTROL_PAUSE) enter_state(APP_MENU);
        else if (now - last_display_ms >= 1000) {
            last_display_ms = now;
            show_session_active();
        }
        break;
    case APP_SESSION_CLOSE:
        if (event == CONTROL_CONFIRM) {
            session_finish(SESSION_COMPLETE, time_us_64());
            manual_session = false;
            enter_state(APP_SESSIONS);
        } else if (event == CONTROL_BACK || event == CONTROL_PAUSE) enter_state(APP_SESSION_ACTIVE);
        break;
    case APP_UNAVAILABLE:
        if (event == CONTROL_BACK) enter_state(APP_MENU);
        break;
    }
}

app_state_t app_get_state(void) {
    return state;
}

void app_update(void) {
    control_event_t event = controls_poll(now_ms());
    app_update_input(event, controls_confirm_state(), time_us_64());
}

void app_update_input(control_event_t event, control_button_state_t button, uint64_t event_us) {
    app_update_captured(event, button, event_us, event_us);
}

void app_update_captured(control_event_t event, control_button_state_t button,
                         uint64_t event_us, uint64_t now_us) {
    app_step(event, button, event_us, now_us);
    bool cognitive = state == APP_MOT || state == APP_RTI || state == APP_PRM || state == APP_PAL;
    communication_mode_t mode = COMM_NORMAL;
    if (cognitive && !assessment_finished()) mode = capturing_cognitive ? COMM_STREAM_ONLY : COMM_SUSPENDED;
    else if (state == APP_CALIBRATING || state == APP_ACQUIRING || state == APP_STOP_CONFIRM)
        mode = COMM_STREAM_ONLY;
    communication_update(mode);
}
