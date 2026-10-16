#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Audio output.
 *
 * Hardware: 16-bit stereo I2S to the PCM5102A DAC.
 *
 * Wokwi: the simulator does not implement I2S on the
 * ESP32-S3, so samples are discarded and the writer is
 * simply delayed for as long as the audio would take to
 * play. Everything upstream (SD reads, MP3 decoding,
 * timing, UI) behaves the same as on hardware.
 */

void audio_out_init(void);

/*
 * Prepares the output for a new sample rate. Also call
 * this after a pause so the Wokwi timing restarts.
 */
void audio_out_start(uint32_t sample_rate);

/*
 * Writes interleaved stereo 16-bit frames (L, R, L, R...).
 * Blocks until the output has room / the audio would
 * have played.
 */
void audio_out_write(const int16_t *frames, size_t frame_count);

/*
 * Drives the DAC's MU pin. Muting while stopped, paused,
 * or changing tracks hides clicks and pops. Starts muted.
 */
void audio_out_set_muted(bool muted);

#endif
