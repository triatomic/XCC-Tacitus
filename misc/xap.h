#pragma once

#include <mmsystem.h>
#include <dsound.h>
#include <virtual_binary.h>

void xap_play(LPDIRECTSOUND, Cvirtual_binary, string);

// Transport controls. Operate on whatever buffer xap_play last started; no-op
// if nothing is playing. Pause/resume keep the worker thread alive across the
// pause (the loop's wait predicate honors a separate pause flag), so the
// resume picks up from the same byte offset DirectSound was at.
void xap_pause();
void xap_resume();
bool xap_is_paused();
// 0..1 ratio of position within the current buffer; -1 if nothing is playing.
// Cheap; safe to call from a UI timer.
double xap_get_progress();
// Total duration of the currently-playing buffer in seconds, or -1 if idle.
double xap_get_duration();
// Seek to a 0..1 ratio of the buffer. Honored whether playing or paused.
void xap_seek(double progress);
// Hard stop (same effect as starting a new file). Idle-safe.
void xap_stop();

inline string xapFilePlaying;
