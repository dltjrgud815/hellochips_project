#pragma once

#include <stdio.h>
#include <alsa/asoundlib.h>

// 초기화 함수
int initialize_mixer();

// 볼륨 조절 함수
void increase_volume();
void decrease_volume();

extern snd_mixer_t *mixer_handle;
extern snd_mixer_elem_t *mixer_elem;

