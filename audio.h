/* Sintetizador de efectos y musica (port del WebAudio del HTML). */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
#define AUDIO_RATE 44100
enum { SFX_PI, SFX_SG, SFX_TIC, SFX_SEL, SFX_DOOR, SFX_PICK, SFX_PAIN, SFX_DIE,
       SFX_MON, SFX_FB, SFX_BOOM, SFX_HURT };
void audio_init(void);
void sfx(int id);                 /* seguro de llamar desde el hilo del juego */
void audio_set_music(int on);
void audio_mix(int16_t *stereo, int frames); /* lo llama el hilo de audio */
#endif
