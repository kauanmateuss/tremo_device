#include <assert.h>
#include <stdio.h>
#include "session.h"
int main(void) {
    session_init(0xabcd);
    session_config_t c = *session_config();
    c.sample_ms = 0; assert(!session_configure(c));
    c.sample_ms = 20; c.duration_ms = 60000; assert(session_configure(c));
    assert(session_begin(SESSION_RTI, 100));
    uint64_t id = session_current()->id;
    assert(id == 0xabcd00000001ull && !session_begin(SESSION_MOT, 200));
    assert(!session_configure(c));
    session_request_pause(); assert(session_current()->pause_requested);
    assert(session_pause(500)); assert(session_active_us(9999) == 400);
    assert(session_resume(1000) == 500); assert(session_active_us(1500) == 900);
    assert(session_pause(2000)); session_finish(SESSION_INTERRUPTED, 4000);
    assert(session_current()->paused_us == 2500 && session_current()->pauses == 2);
    assert(session_active_us(9999) == 1400);
    session_finish(SESSION_COMPLETE, 8000); assert(session_current()->state == SESSION_INTERRUPTED);
    assert(session_begin(SESSION_PAL, 9000)); assert(session_current()->id == id + 1);
    assert(session_current()->config.duration_ms == 60000 && session_current()->pauses == 0);
    puts("PASS sessoes: IDs, configuracao, pausa, retomada e termino");
}
