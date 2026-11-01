#include "pal.h"
#include "patterns.h"
#include "matrix.h"
#include "display.h"
#include "pico/stdlib.h"
#include <stdio.h>

static pal_state_t state;
static pal_result_t result;
static bool training, saved, usb_connected;
static uint8_t deck[PATTERN_COUNT], patterns[PAL_MAX_PAIRS], positions[PAL_MAX_PAIRS], order[PAL_MAX_PAIRS];
static uint8_t pairs, round_number, encoding, trial, round_correct, cursor, deck_offset;
static uint64_t state_started_us, onset_us;

static void corner(uint8_t position, bool selected) {
    npSetPixel((position % 2) * 4, (position / 2) * 4, selected ? 20 : 0, selected ? 20 : 0, 20);
}

static void draw_choice(void) {
    patterns_draw(patterns[order[trial]]);
    for (uint8_t p = 0; p < PAL_MAX_PAIRS; ++p) corner(p, p == cursor);
    npWrite(); // Um único envio; nenhuma cor indica a resposta correta.
}

static void export_result(void) {
    const char *names[] = {"correct", "wrong", "omission", "aborted"};
    printf("# PAL_ADAPTADO_V1 interrupted=%u limit=%u levels=%u rounds=%u correct=%u wrong=%u omissions=%u\n",
           result.interrupted, result.limit_reached, result.levels_completed, result.rounds_started,
           result.correct, result.wrong, result.omissions);
    printf("pal,phase,pairs,round,trial,pattern,target_position,selected_position,outcome,onset_us,elapsed_us\n");
    for (uint8_t i = 0; i < result.record_count; ++i) {
        const pal_record_t *r = &result.records[i];
        char selection[4] = "";
        if (r->selected_position != PAL_NO_POSITION) snprintf(selection, sizeof(selection), "%u", r->selected_position);
        printf("PAL,%s,%u,%u,%u,%u,%u,%s,%s,%llu,%llu\n", r->training ? "training" : "test",
               r->pairs, r->round, r->trial, r->pattern, r->target_position, selection, names[r->outcome],
               (unsigned long long)r->onset_us, (unsigned long long)r->elapsed_us);
    }
    printf("# PAL_END records=%u\n", result.record_count);
}

static void finish(bool interrupted) {
    result.interrupted = interrupted;
    saved = true;
    state = PAL_RESULTS;
    patterns_clear();
    char levels[20], correct[16], wrong[16], omissions[16], rounds[20];
    snprintf(levels, sizeof(levels), "NIVEIS %u DE 3", result.levels_completed);
    snprintf(correct, sizeof(correct), "ACERTOS %u", result.correct);
    snprintf(wrong, sizeof(wrong), "ERROS %u", result.wrong);
    snprintf(omissions, sizeof(omissions), "OMISSOES %u", result.omissions);
    snprintf(rounds, sizeof(rounds), "RODADAS %u", result.rounds_started);
    const char *lines[] = {"PAL ADAPTADO", interrupted ? "INTERROMPIDO" : result.limit_reached ? "LIMITE ATINGIDO" : "CONCLUIDO",
        levels, correct, wrong, omissions, rounds, "A OU B MENU"};
    display_show_lines(lines, 8);
    usb_connected = stdio_usb_connected();
    if (usb_connected) export_result();
}

static void shuffle_order(void) {
    for (uint8_t i = 0; i < pairs; ++i) order[i] = i;
    patterns_shuffle(order, pairs);
}

static void show_encoding(void) {
    char progress[24];
    snprintf(progress, sizeof(progress), "PAR %u DE %u", encoding + 1, pairs);
    const char *lines[] = {training ? "TREINO PAL" : "PAL", "MEMORIZE FORMA", "E CANTO AZUL", progress, "", "B ENCERRA"};
    display_show_lines(lines, 6);
    uint8_t pair = order[encoding];
    patterns_draw(patterns[pair]);
    corner(positions[pair], false);
    npWrite();
    state_started_us = time_us_64();
    state = PAL_ENCODING;
}

static void start_round(void) {
    encoding = trial = round_correct = 0;
    if (!training) ++result.rounds_started;
    shuffle_order();
    show_encoding();
}

static void start_level(void) {
    uint8_t corners[] = {0, 1, 2, 3};
    patterns_shuffle(corners, PAL_MAX_PAIRS);
    for (uint8_t i = 0; i < pairs; ++i) {
        patterns[i] = deck[deck_offset + i];
        positions[i] = corners[i];
    }
    deck_offset += pairs;
    round_number = 1;
    start_round();
}

static void start_choice(void) {
    char progress[24];
    snprintf(progress, sizeof(progress), "%u PARES ROD %u", pairs, round_number);
    const char *lines[] = {training ? "TREINO PAL" : "PAL", "QUAL ERA CANTO?", progress, "CURSOR BRANCO", "MOVA JOYSTICK", "A CONFIRMA", "LIMITE 15 S", "B ENCERRA"};
    display_show_lines(lines, 8);
    cursor = 0;
    draw_choice();
    onset_us = time_us_64();
    state = PAL_CHOICE;
}

static void record_choice(pal_outcome_t outcome, uint64_t event_us) {
    uint8_t pair = order[trial];
    pal_record_t *r = &result.records[result.record_count++];
    *r = (pal_record_t){.training = training, .pairs = pairs, .round = round_number, .trial = trial + 1,
        .pattern = patterns[pair], .target_position = positions[pair],
        .selected_position = outcome == PAL_CORRECT || outcome == PAL_WRONG ? cursor : PAL_NO_POSITION,
        .outcome = outcome, .onset_us = onset_us, .elapsed_us = event_us - onset_us};
    if (outcome == PAL_CORRECT) ++round_correct;
    if (!training) {
        if (outcome == PAL_CORRECT) { ++result.correct; result.correct_time_us += r->elapsed_us; }
        else if (outcome == PAL_WRONG) ++result.wrong;
        else if (outcome == PAL_OMISSION) ++result.omissions;
    }
}

static void end_choice(pal_outcome_t outcome, uint64_t event_us) {
    record_choice(outcome, event_us);
    patterns_clear();
    const char *feedback = outcome == PAL_CORRECT ? "ACERTOU" : outcome == PAL_WRONG ? "ERROU" : "TEMPO ESGOTADO";
    const char *lines[] = {training ? "TREINO PAL" : "PAL", training ? feedback : "AGUARDE", "SOLTE CONTROLES"};
    display_show_lines(lines, 3);
    state_started_us = time_us_64();
    state = PAL_INTERVAL;
}

static void end_round(void) {
    if (training) {
        bool passed = round_correct == pairs;
        const char *lines[] = {passed ? "TREINO OK" : "REPETIR TREINO", "ACERTE OS PARES", "", passed ? "A INICIAR" : "A REPETIR", "B ENCERRA"};
        display_show_lines(lines, 5);
        state = passed ? PAL_READY : PAL_TRAINING_FAILED;
    } else if (round_correct == pairs) {
        ++result.levels_completed;
        if (pairs == PAL_MAX_PAIRS) finish(false);
        else {
            const char *lines[] = {"NIVEL CONCLUIDO", "MAIS UM PAR", "", "A PROXIMO NIVEL", "B ENCERRA"};
            display_show_lines(lines, 5);
            state = PAL_NEXT_LEVEL;
        }
    } else if (round_number < PAL_MAX_ROUNDS) {
        const char *lines[] = {"REVER OS PARES", "MESMAS POSICOES", "", "A REPETIR", "B ENCERRA"};
        display_show_lines(lines, 5);
        state = PAL_RETRY;
    } else { result.limit_reached = true; finish(false); }
}

void pal_begin(void) {
    result = (pal_result_t){0};
    saved = false;
    training = true;
    pairs = PAL_TRAINING_PAIRS;
    deck_offset = 0;
    for (uint8_t i = 0; i < PATTERN_COUNT; ++i) deck[i] = i;
    patterns_shuffle(deck, PATTERN_COUNT);
    patterns_clear();
    const char *lines[] = {"PAL ADAPTADO", "MEMORIZE FORMA", "E CANTO AZUL", "DEPOIS INDIQUE", "CURSOR BRANCO", "A CONFIRMA", "B ENCERRA", "A TREINAR"};
    display_show_lines(lines, 8);
    state = PAL_INSTRUCTIONS;
}

bool pal_update(control_event_t event, uint64_t event_us) {
    if (state == PAL_RESULTS) return event == CONTROL_CONFIRM || event == CONTROL_BACK;
    if (event == CONTROL_BACK) {
        if (state == PAL_CHOICE) record_choice(PAL_ABORTED, event_us);
        finish(true);
        return false;
    }
    switch (state) {
    case PAL_INSTRUCTIONS:
        if (event == CONTROL_CONFIRM) start_level();
        break;
    case PAL_ENCODING:
        if (event_us - state_started_us >= PAL_ENCODING_US) {
            patterns_clear(); state_started_us = time_us_64(); state = PAL_GAP;
        }
        break;
    case PAL_GAP:
        if (event_us - state_started_us >= PAL_GAP_US) {
            if (++encoding < pairs) show_encoding();
            else { state_started_us = time_us_64(); state = PAL_RETENTION; }
        }
        break;
    case PAL_RETENTION:
        if (event_us - state_started_us >= PAL_RETENTION_US) {
            shuffle_order(); start_choice(); // Ordem de consulta independente da exposição.
        }
        break;
    case PAL_CHOICE:
        if (event_us - onset_us >= PAL_TIMEOUT_US) end_choice(PAL_OMISSION, event_us);
        else if (event == CONTROL_CONFIRM) end_choice(cursor == positions[order[trial]] ? PAL_CORRECT : PAL_WRONG, event_us);
        else {
            uint8_t previous = cursor;
            if (event == CONTROL_LEFT) cursor &= ~1u;
            else if (event == CONTROL_RIGHT) cursor |= 1u;
            else if (event == CONTROL_UP) cursor &= ~2u;
            else if (event == CONTROL_DOWN) cursor |= 2u;
            if (cursor != previous) draw_choice();
        }
        break;
    case PAL_INTERVAL:
        if (event_us - state_started_us < PAL_INTERVAL_US) break;
        if (++trial < pairs) start_choice();
        else end_round();
        break;
    case PAL_READY:
        if (event == CONTROL_CONFIRM) { training = false; pairs = 2; start_level(); }
        break;
    case PAL_TRAINING_FAILED:
        if (event == CONTROL_CONFIRM) { result = (pal_result_t){0}; start_round(); }
        break;
    case PAL_RETRY:
        if (event == CONTROL_CONFIRM) { ++round_number; start_round(); }
        break;
    case PAL_NEXT_LEVEL:
        if (event == CONTROL_CONFIRM) { ++pairs; start_level(); }
        break;
    case PAL_RESULTS:
        break;
    }
    return false;
}

void pal_serial_update(void) {
    bool connected = stdio_usb_connected();
    if (saved && connected && !usb_connected) export_result();
    usb_connected = connected;
}
pal_state_t pal_get_state(void) { return state; }
const pal_result_t *pal_result(void) { return &result; }
