#include "mixer_control.h"

snd_mixer_t *mixer_handle = NULL;
snd_mixer_elem_t *mixer_elem = NULL;

int initialize_mixer() {
    int err;

    err = snd_mixer_open(&mixer_handle, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open mixer: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_mixer_attach(mixer_handle, "hw:1");  // USB 오디오 장치
    if (err < 0) {
        fprintf(stderr, "Failed to attach mixer: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_mixer_selem_register(mixer_handle, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "Failed to register mixer: %s\n", snd_strerror(err));
        return -1;
    }

    err = snd_mixer_load(mixer_handle);
    if (err < 0) {
        fprintf(stderr, "Failed to load mixer: %s\n", snd_strerror(err));
        return -1;
    }

    mixer_elem = snd_mixer_first_elem(mixer_handle);
    while (mixer_elem != NULL) {
        if (snd_mixer_selem_has_playback_volume(mixer_elem)) {
            break;
        }
        mixer_elem = snd_mixer_elem_next(mixer_elem);
    }

    if (mixer_elem == NULL) {
        fprintf(stderr, "No playback volume control found\n");
        return -1;
    }

    return 0;
}

void increase_volume() {
    long volume, min, max;

    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
    snd_mixer_selem_get_playback_volume(mixer_elem, 0, &volume);

    if (volume < max) {
        volume += (max - min) / 5; 
        if (volume > max) {
            volume = max;
        }
        snd_mixer_selem_set_playback_volume(mixer_elem, 0, volume);
        printf("Volume increased to %ld\n", volume);
    } else {
        printf("Maximum volume reached\n");
    }
}

void decrease_volume() {
    long volume, min, max;

    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
    snd_mixer_selem_get_playback_volume(mixer_elem, 0, &volume);

    if (volume > min) {
        volume -= (max - min) / 5;  //
        if (volume < min) {
            volume = min;
        }
        snd_mixer_selem_set_playback_volume(mixer_elem, 0, volume);
        printf("Volume decreased to %ld\n", volume);
    } else {
        printf("Minimum volume reached\n");
    }
}