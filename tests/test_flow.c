#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "session.h"
#include "evaluation.h"
#include "controls.h"
#include "monitoring.h"
#include "mpu6050_i2c.h"
#include "matrix.h"
#include "mot.h"
#include "rti.h"
#include "prm.h"
#include "pal.h"

static uint64_t clock_ms;
static control_event_t input;
static control_button_state_t button = {.settled = true};
control_button_state_t controls_confirm_state(void) { return button; }
uint32_t get_rand_32(void) { return 0; }
static bool healthy[2] = {true, false};
static bool usb;
static bool matrix_ok = true;
static unsigned int reads;
static char screen[8][64], saved_screen[8][64];
void display_save_screen(void) { memcpy(saved_screen, screen, sizeof(screen)); }
void display_restore_screen(void) { memcpy(screen, saved_screen, sizeof(screen)); }

uint64_t time_us_64(void) { return (uint64_t)clock_ms * 1000; }
void npClear(void) {}
void npWrite(void) {}
void npSetPixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b) { (void)x; (void)y; (void)r; (void)g; (void)b; }
void stdio_init_all(void) {}
bool stdio_usb_connected(void) { return usb; }
absolute_time_t get_absolute_time(void) { return clock_ms; }
uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)t; }
void controls_init(void) {}
control_event_t controls_poll(uint32_t now) { (void)now; control_event_t e = input; input = CONTROL_NONE; return e; }
void display_init(void) {}
void display_draw_text(const char *t, uint8_t x, uint8_t y) { (void)t; (void)x; (void)y; }
void display_render(void) {}
void display_show_lines(const char *text[], size_t n) {
    assert(n <= 8);
    memset(screen, 0, sizeof(screen));
    for (size_t i = 0; i < n; ++i) {
        if (strlen(text[i]) > 15) fprintf(stderr, "Linha longa: %s\n", text[i]);
        assert(strlen(text[i]) <= 15); // Não pode cortar instruções ou números.
        strcpy(screen[i], text[i]);
    }
}
bool npInit(uint pin) { assert(pin == LED_PIN); return matrix_ok; }
void mpu6050_setup_i2c(void) {}
bool mpu6050_configure(uint8_t addr, uint16_t ms, char error[MPU6050_DIAG_SIZE]) { (void)error; assert(ms == 20 || ms == 40); return healthy[addr - 0x68]; }
bool mpu6050_reset(uint8_t addr) { assert(addr == 0x68 || addr == 0x69); return healthy[addr - 0x68]; }
bool mpu6050_set_accel_range(uint8_t range, uint8_t addr) { assert(range == 0); return healthy[addr - 0x68]; }
bool mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], uint8_t addr) {
    ++reads;
    if (!healthy[addr - 0x68]) return false;
    if (app_get_state() == APP_CALIBRATING) {
        accel[0] = accel[1] = gyro[0] = gyro[1] = gyro[2] = 0; accel[2] = 16384; return true;
    }
    accel[0] = -32768; accel[1] = 32767; accel[2] = 16384;
    gyro[0] = -32768; gyro[1] = 32767; gyro[2] = 0;
    return true;
}
static void step(control_event_t e, uint32_t elapsed) {
    input = e;
    clock_ms += elapsed;
    if (e == CONTROL_CONFIRM || e == CONTROL_CONFIRM_RELEASE) {
        button.pressed = e == CONTROL_CONFIRM;
        button.edge_us = (uint64_t)clock_ms * 1000;
    }
#ifdef TEST_TASK_INPUT
    control_button_state_t captured = button;
    button = (control_button_state_t){0}; // A FSM deve usar a cópia recebida.
    app_update_input(e, captured, time_us_64());
    button = captured;
#else
    app_update();
#endif
}
static void finish_calibration(void) {
    assert(app_get_state() == APP_CALIBRATING);
    step(CONTROL_NONE, 300);
    for (unsigned i = 1; i < MONITORING_CAL_SAMPLES; ++i) step(CONTROL_NONE, session_config()->sample_ms);
    assert(app_get_state() == APP_READY);
}
static void start_from_menu(void) {
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_INSTRUCTIONS);
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_PREPARING);
    step(CONTROL_NONE, 5); finish_calibration();
    unsigned int before = reads;
    step(CONTROL_NONE, 100); assert(reads == before);
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_ACQUIRING);
}
static void complete_mot(void) {
    step(CONTROL_CONFIRM, 5);
    const control_event_t moves[] = {CONTROL_LEFT, CONTROL_UP, CONTROL_RIGHT};
    for (unsigned i = 0; i < 3; ++i) {
        step(moves[i], 50); step(CONTROL_CONFIRM, 50); step(CONTROL_NONE, 1000);
    }
    assert(mot_get_state() == MOT_READY); step(CONTROL_CONFIRM, 5);
    for (unsigned i = 0; i < 10; ++i) { step(CONTROL_NONE, 10000); step(CONTROL_NONE, 1000); }
    assert(mot_get_state() == MOT_RESULTS);
}
static void complete_rti(void) {
    step(CONTROL_CONFIRM, 5);
    for (unsigned i = 0; i < 3; ++i) {
        step(CONTROL_CONFIRM_RELEASE, 50); step(CONTROL_CONFIRM, 50);
        step(CONTROL_NONE, 1000); step(CONTROL_CONFIRM_RELEASE, 250); step(CONTROL_NONE, 1000);
    }
    assert(rti_get_state() == RTI_READY); step(CONTROL_CONFIRM, 5);
    for (unsigned i = 0; i < 10; ++i) {
        step(CONTROL_CONFIRM_RELEASE, 50); step(CONTROL_CONFIRM, 50);
        step(CONTROL_NONE, 1000); step(CONTROL_NONE, 2000); step(CONTROL_NONE, 1000);
    }
    assert(rti_get_state() == RTI_RESULTS);
}
static void complete_tremor(void) {
    assert(app_get_state() == APP_INSTRUCTIONS);
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 5); finish_calibration();
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 0);
    assert(app_get_state() == APP_ACQUIRING && monitoring_result()->valid[0] == 1);
    step(CONTROL_NONE, 30000); assert(app_get_state() == APP_RESULTS);
}
static void exercise_evaluations(void) {
    matrix_ok = true; healthy[0] = healthy[1] = true;
    for (unsigned mode = 0; mode < 3; ++mode) {
        app_init(); step(CONTROL_NONE, 2000);
        session_config_t c = *session_config(); c.tests_mask = mode == 0 ? 3 : 2; c.tremor_first = mode == 0;
        assert(session_configure(c));
        step(CONTROL_DOWN, 5);
        if (mode < 2) step(CONTROL_DOWN, 5);
        step(CONTROL_CONFIRM, 5);
        if (mode == 2) { step(CONTROL_UP, 5); step(CONTROL_CONFIRM, 5); }
        assert(app_get_state() == APP_EVAL_SETUP);
        uint64_t id = session_current()->id;
        step(CONTROL_CONFIRM, 5);
        unsigned count = evaluation_current()->count;
        for (unsigned i = 0; i < count; ++i) {
            unsigned module = evaluation_current()->steps[i].module;
            unsigned before = reads;
            if (module == 4) { complete_tremor(); assert(monitoring_result()->session_id == id); }
            else if (module == 0) { complete_mot(); assert(mot_result()->session_id == id && reads == before); }
            else { complete_rti(); assert(rti_result()->session_id == id && reads == before); }
            assert(evaluation_current()->steps[i].status == STEP_COMPLETE);
            step(CONTROL_CONFIRM, 5);
        }
        assert(app_get_state() == APP_EVAL_RESULTS && session_current()->state == SESSION_COMPLETE);
        assert(!evaluation_current()->active && evaluation_current()->session_id == id);
    }
    // Falha de MPU antes da coleta interrompe a sequência; etapas futuras ficam pendentes.
    app_init(); step(CONTROL_NONE, 2000); healthy[1] = false;
    step(CONTROL_DOWN, 5); step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    step(CONTROL_CONFIRM, 5); step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 5);
    assert(app_get_state() == APP_EVAL_RESULTS && session_current()->state == SESSION_ERROR);
    assert(evaluation_current()->steps[0].status == STEP_ERROR && evaluation_current()->steps[1].status == STEP_PENDING);
    healthy[1] = true; app_init(); step(CONTROL_NONE, 2000);
    step(CONTROL_DOWN, 5); step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5); step(CONTROL_CONFIRM, 5);
    complete_tremor(); step(CONTROL_CONFIRM, 5); // MOT.
    step(CONTROL_BACK, 5);
    assert(app_get_state() == APP_EVAL_RESULTS && session_current()->state == SESSION_INTERRUPTED);
    assert(evaluation_current()->steps[0].status == STEP_COMPLETE && evaluation_current()->steps[1].status == STEP_INTERRUPTED);
}
int main(void) {
    app_init();
    assert(app_get_state() == APP_WELCOME);
    assert(strcmp(screen[5], "MPU2 FALHA") == 0);
    usb = true; step(CONTROL_NONE, 0); // Conexão após a falha no boot.
    usb = false;
    unsigned int boot_reads = reads;
    step(CONTROL_CONFIRM, 1999); assert(app_get_state() == APP_WELCOME);
    step(CONTROL_NONE, 1); assert(app_get_state() == APP_MENU);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_MENU);
    assert(reads == boot_reads); // Nada de aquisição no menu.
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_INSTRUCTIONS);
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 5);
    assert(app_get_state() == APP_SENSOR_ERROR);
    step(CONTROL_NONE, 100); assert(app_get_state() == APP_SENSOR_ERROR);
    healthy[1] = true;
    step(CONTROL_CONFIRM, 5); step(CONTROL_NONE, 5);
    finish_calibration();
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_INSTRUCTIONS);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_MENU);
    // Todos os itens, incluindo a volta circular do joystick.
    step(CONTROL_UP, 5); step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_INFO);
    step(CONTROL_BACK, 5); step(CONTROL_DOWN, 5);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_COG_MENU);
    unsigned int before_mot = reads;
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_MOT);
    step(CONTROL_CONFIRM, 5); assert(mot_get_state() == MOT_STIMULUS);
    step(CONTROL_PAUSE, 5); assert(session_current()->pause_requested);
    step(CONTROL_LEFT, 50); step(CONTROL_CONFIRM, 50);
    assert(session_current()->state == SESSION_PAUSED && mot_get_state() == MOT_INTERVAL);
    step(CONTROL_NONE, 5000); assert(mot_get_state() == MOT_INTERVAL);
    step(CONTROL_CONFIRM, 5); assert(session_current()->state == SESSION_RUNNING);
    assert(mot_result()->session_id == session_current()->id);
    step(CONTROL_NONE, 1000); step(CONTROL_BACK, 50);
    assert(mot_get_state() == MOT_RESULTS);
    assert(reads == before_mot); // Nenhuma leitura MPU durante o MOT.
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_RTI);
    unsigned int before_rti = reads;
    step(CONTROL_CONFIRM, 5);
    step(CONTROL_CONFIRM_RELEASE, 50);
    step(CONTROL_CONFIRM, 50); assert(rti_get_state() == RTI_FOREPERIOD);
    step(CONTROL_NONE, 1000); assert(rti_get_state() == RTI_STIMULUS);
    step(CONTROL_CONFIRM_RELEASE, 250);
    step(CONTROL_BACK, 50); assert(rti_get_state() == RTI_RESULTS);
    assert(reads == before_rti);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_PRM);
    unsigned int before_prm = reads;
    step(CONTROL_CONFIRM, 5); assert(prm_get_state() == PRM_ENCODING);
    step(CONTROL_NONE, 2000);
    step(CONTROL_BACK, 5); assert(prm_get_state() == PRM_RESULTS);
    assert(reads == before_prm);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_PAL);
    unsigned int before_pal = reads;
    step(CONTROL_CONFIRM, 5); assert(pal_get_state() == PAL_ENCODING);
    step(CONTROL_NONE, 2000);
    step(CONTROL_BACK, 5); assert(pal_get_state() == PAL_RESULTS);
    assert(reads == before_pal);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_EVAL_SETUP);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_EVAL_RESULTS);
    step(CONTROL_BACK, 5); step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(strcmp(screen[0], "AVAL COMPLETA") == 0);
    step(CONTROL_BACK, 5); step(CONTROL_BACK, 5); step(CONTROL_DOWN, 5); step(CONTROL_DOWN, 5);
    start_from_menu();
    step(CONTROL_NONE, 0);
    assert(strcmp(screen[1], "X-2000 -250.1") == 0);
    assert(monitoring_result()->valid[0] == 1);
    step(CONTROL_NONE, 39); assert(monitoring_result()->valid[0] == 1);
    step(CONTROL_NONE, 1); assert(monitoring_result()->valid[0] == 2);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_STOP_CONFIRM);
    step(CONTROL_NONE, 200);
    assert(strcmp(screen[0], "ENCERRAR") == 0); // Aquisição não sobrescreve diálogo.
    assert(monitoring_result()->valid[0] == 3);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_ACQUIRING);
    step(CONTROL_BACK, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_RESULTS);
    assert(monitoring_result()->reason == MONITORING_INTERRUPTED);
    unsigned int stopped_reads = reads;
    step(CONTROL_NONE, 1000); assert(reads == stopped_reads);
    usb = true; step(CONTROL_NONE, 5); // Publica resumo retido em RAM.
    usb = false; step(CONTROL_CONFIRM, 5);
    start_from_menu();
    assert(monitoring_result()->valid[0] == 0); // Nova sessão limpa contagens.
    step(CONTROL_PAUSE, 0); assert(app_get_state() == APP_PAUSED);
    unsigned paused_reads = reads;
    step(CONTROL_NONE, 5000); assert(reads == paused_reads);
    step(CONTROL_CONFIRM, 0); assert(app_get_state() == APP_ACQUIRING);
    step(CONTROL_NONE, 0);
    for (uint32_t elapsed = 5; elapsed < MONITORING_DURATION_MS; elapsed += 5)
        step(CONTROL_NONE, 5);
    assert(monitoring_result()->valid[0] == 750);
    step(CONTROL_NONE, 5);
    assert(app_get_state() == APP_RESULTS);
    assert(monitoring_result()->reason == MONITORING_COMPLETE);
    assert(monitoring_result()->duration_ms == MONITORING_DURATION_MS);
    step(CONTROL_BACK, 5);
    start_from_menu();
    healthy[1] = false;
    step(CONTROL_NONE, 0);
    assert(app_get_state() == APP_RESULTS);
    assert(monitoring_result()->reason == MONITORING_SENSOR_ERROR);
    assert(monitoring_result()->valid[0] == 1 && monitoring_result()->errors[1] == 1);
    stopped_reads = reads;
    step(CONTROL_NONE, 500); assert(reads == stopped_reads);
    healthy[1] = true;
    step(CONTROL_BACK, 5);
    start_from_menu();
    step(CONTROL_BACK, 0);
    assert(app_get_state() == APP_STOP_CONFIRM);
    step(CONTROL_NONE, MONITORING_DURATION_MS);
    assert(app_get_state() == APP_RESULTS);
    assert(monitoring_result()->reason == MONITORING_COMPLETE);
    // A subtração de tempos também funciona quando o contador de ms dá a volta.
    step(CONTROL_BACK, 5);
    clock_ms = UINT32_MAX - 100;
    start_from_menu();
    step(CONTROL_NONE, 0);
    step(CONTROL_NONE, MONITORING_DURATION_MS);
    assert(app_get_state() == APP_RESULTS);
    assert(monitoring_result()->duration_ms == MONITORING_DURATION_MS);
    matrix_ok = false;
    app_init(); step(CONTROL_NONE, 2000);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    step(CONTROL_CONFIRM, 5); assert(app_get_state() == APP_MOT_ERROR);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_RTI_ERROR);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_PRM_ERROR);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    step(CONTROL_DOWN, 5); step(CONTROL_CONFIRM, 5);
    assert(app_get_state() == APP_PAL_ERROR);
    step(CONTROL_BACK, 5); assert(app_get_state() == APP_COG_MENU);
    exercise_evaluations();
    puts("PASS FSM: menu, preparo, inicio, interrupcao, conclusao, falha e resultados");
    return 0;
}
