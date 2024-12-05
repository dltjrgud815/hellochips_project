#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <mpg123.h>
#include <alsa/asoundlib.h>

#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

typedef struct {
    int motor;
    int fan_motor;
    int distance_sensor;
    int light_sensor;
    int tmp_sensor;
} state_info;

typedef struct {
    int sock;
    struct sockaddr_in addr;
} client_info;

state_info global_state = { .motor = -1, .distance_sensor = -1 }; // 초기 상태

pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t external_mutex = PTHREAD_MUTEX_INITIALIZER;

// Playlist variables
const char *playlist[] = {
    "./music/song1.mp3",
    "./music/song2.mp3",
    "./music/song3.mp3"
};

const char *coment[] = {
    "./music/start.mp3",
    "./music/receive_command.mp3",
    "./music/light_on.mp3",
    "./music/light_off.mp3",
    "./music/front_danger.mp3",
    "./music/back_danger.mp3",
    "./music/fan_on.mp3",
    "./music/fan_off.mp3",
    "./music/alert.mp3"
};

volatile int current_song = 0;
volatile int is_paused = 0;
volatile int song_change_request = 0;
int num_files = sizeof(playlist) / sizeof(playlist[0]);

mpg123_handle *mpg_handle = NULL;
snd_mixer_t *mixer_handle = NULL;
snd_mixer_elem_t *mixer_elem = NULL;

// ALSA initialization for audio playback
snd_pcm_t *initialize_alsa(const char *device, long rate, int channels) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hw_params;

    if (snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", device);
        return NULL;
    }

    snd_pcm_hw_params_malloc(&hw_params);
    snd_pcm_hw_params_any(handle, hw_params);
    snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate(handle, hw_params, rate, 0);
    snd_pcm_hw_params_set_channels(handle, hw_params, channels);

    snd_pcm_hw_params(handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    snd_pcm_prepare(handle);
    return handle;
}

int initialize_mixer() {
    int err;

    // "hw:1" 장치를 사용하여 믹서를 엽니다 (AB13X USB Audio 장치)
    err = snd_mixer_open(&mixer_handle, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open mixer: %s\n", snd_strerror(err));
        return -1;
    }

    // "hw:1" 장치로 믹서를 연결합니다 (AB13X USB Audio)
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

    // 볼륨 제어를 위한 믹서 요소 찾기
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

void play_mp3(const char *filename) {
    mpg123_handle *mpg_handle;
    snd_pcm_t *playback_handle;
    unsigned char *buffer;
    size_t buffer_size, done;
    int err;
    int channels, encoding;
    long rate;

    if (mpg123_init() != MPG123_OK || (mpg_handle = mpg123_new(NULL, &err)) == NULL) {
        fprintf(stderr, "Failed to initialize mpg123: %s\n", mpg123_plain_strerror(err));
        return;
    }

    if (mpg123_open(mpg_handle, filename) != MPG123_OK) {
        fprintf(stderr, "Failed to open MP3 file: %s\n", filename);
        mpg123_delete(mpg_handle);
        mpg123_exit();
        return;
    }

    mpg123_getformat(mpg_handle, &rate, &channels, &encoding);
    playback_handle = initialize_alsa("plughw:1,0", rate, channels);
    if (!playback_handle) {
        mpg123_close(mpg_handle);
        mpg123_delete(mpg_handle);
        mpg123_exit();
        return;
    }

    buffer_size = mpg123_outblock(mpg_handle);
    buffer = (unsigned char *)malloc(buffer_size);

    while (1) {
        pthread_mutex_lock(&audio_mutex);
        if (song_change_request) {
            pthread_mutex_unlock(&audio_mutex);
            snd_pcm_drop(playback_handle);
            break;
        }
        pthread_mutex_unlock(&audio_mutex);

        if (mpg123_read(mpg_handle, buffer, buffer_size, &done) != MPG123_OK) {
            break;
        }

        if (is_paused) {
            snd_pcm_pause(playback_handle, 1);
            while (is_paused) usleep(10000);
            snd_pcm_pause(playback_handle, 0);
        }

        err = snd_pcm_writei(playback_handle, buffer, done / (2 * channels));
        if (err == -EPIPE) {
            fprintf(stderr, "Buffer underrun occurred\n");
            snd_pcm_prepare(playback_handle);
        } else if (err < 0) {
            fprintf(stderr, "Playback error: %s\n", snd_strerror(err));
            break;
        }
    }

    free(buffer);
    snd_pcm_drain(playback_handle);
    snd_pcm_close(playback_handle);
    mpg123_close(mpg_handle);
    mpg123_delete(mpg_handle);
    mpg123_exit();
}

// 볼륨 증가 함수
void increase_volume() {
    long volume, min, max;

    // 볼륨 범위 얻기
    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);

    // 현재 볼륨 가져오기
    snd_mixer_selem_get_playback_volume(mixer_elem, 0, &volume);

    // 볼륨 증가 (최대값을 넘지 않도록 제한)
    if (volume < max) {
        volume += (max - min) / 10;  // 10% 증가
        if (volume > max) {
            volume = max;
        }
        snd_mixer_selem_set_playback_volume(mixer_elem, 0, volume);
        printf("Volume increased to %ld\n", volume);
    } else {
        printf("Maximum volume reached\n");
    }
}

// 볼륨 감소 함수
void decrease_volume() {
    long volume, min, max;

    // 볼륨 범위 얻기
    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);

    // 현재 볼륨 가져오기
    snd_mixer_selem_get_playback_volume(mixer_elem, 0, &volume);

    // 볼륨 감소 (최소값을 넘지 않도록 제한)
    if (volume > min) {
        volume -= (max - min) / 10;  // 10% 감소
        if (volume < min) {
            volume = min;
        }
        snd_mixer_selem_set_playback_volume(mixer_elem, 0, volume);
        printf("Volume decreased to %ld\n", volume);
    } else {
        printf("Minimum volume reached\n");
    }
}

void *audio_playback_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&audio_mutex);
        int song_index = current_song;
        song_change_request = 0;
        pthread_mutex_unlock(&audio_mutex);

        while (is_paused) {
            usleep(10000);
        }

        play_mp3(playlist[song_index]);

        pthread_mutex_lock(&audio_mutex);
        if (!song_change_request) {
            current_song = (current_song + 1) % num_files;
        }
        pthread_mutex_unlock(&audio_mutex);
    }
    return NULL;
}

void external_check_and_update_state(int motor_direction, int distance_sensor) {
    pthread_mutex_lock(&external_mutex); 

    if (global_state.motor != motor_direction || global_state.distance_sensor != distance_sensor) {
        printf("Motor direction: %d\n", motor_direction);
        printf("Sensor status:   %d\n", distance_sensor);

        if(motor_direction==1 && distance_sensor==1){
            printf("Front Object Detection..!");
        }
        if(motor_direction==1 && distance_sensor==2){
            printf("Front Object Danger..!");
        }
        if(motor_direction==2 && distance_sensor==1){
            printf("Back Object Detection..!");
        }
        if(motor_direction==2 && distance_sensor==2){
            printf("Back Object Danger..!");
        }

        // 전역 상태 업데이트
        global_state.motor = motor_direction;
        global_state.distance_sensor = distance_sensor;
    }

    pthread_mutex_unlock(&external_mutex); // 상태 접근 해제
}

void *handle_unidirectional(void *arg) {
    client_info *cli = (client_info *)arg;
    char buffer[BUFFER_SIZE];
    int motor_direction, distance_sensor;

    while (1) {
        int recv_len = recv(cli->sock, buffer, BUFFER_SIZE - 1, 0);
        if (recv_len <= 0) {
            printf("External D3 disconnected\n");
            break;
        }
        buffer[recv_len] = '\0';
        printf("Received from External D3: %s\n", buffer);

        if (sscanf(buffer, "%d %d", &motor_direction, &distance_sensor) == 2) {
            external_check_and_update_state(motor_direction, distance_sensor); // 상태 변화 확인 및 출력
        } else {
            printf("Invalid data received: %s\n", buffer);
        }
    }
    close(cli->sock);
    free(cli);
    return NULL;
}

void *handle_bidirectional(void *arg) {
    client_info *cli = (client_info *)arg;
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (1) {
        int recv_len = recv(cli->sock, buffer, BUFFER_SIZE - 1, 0);
        int category = 0, cmd = 0;

        if (recv_len <= 0) {
            printf("Client D3 disconnected\n");
            break;
        }
        buffer[recv_len] = '\0';
        printf("Received from Client D3: %s\n", buffer);

        if (sscanf(buffer, "%d %d", &category, &cmd) < 2) {
            snprintf(response, BUFFER_SIZE, "Invalid command format: %.999s", buffer);
        } else {
            pthread_mutex_lock(&audio_mutex);

            switch (category) {
                case 1:
                    printf("Client call!\n");
                    break;

                case 2:
                    printf("Fan Motor CMD\n");
                    break;

                case 3:
                    printf("Light CMD\n");
                    break;

                case 4:
                    printf("Media CMD\n");
                    switch (cmd) {
                        case 1: // 토글 재생/일시정지
                            is_paused = !is_paused;
                            snprintf(response, BUFFER_SIZE - 1, "Playback is now %s", is_paused ? "paused" : "resumed");
                            break;

                        case 2: // 다음 곡
                            current_song = (current_song + 1) % num_files;
                            song_change_request = 1;
                            is_paused = 0;
                            snprintf(response, BUFFER_SIZE - 1, "Playing next song: %s", playlist[current_song]);
                            break;

                        case 3: // 이전 곡
                            current_song = (current_song - 1 + num_files) % num_files;
                            song_change_request = 1;
                            is_paused = 0;
                            snprintf(response, BUFFER_SIZE - 1, "Playing previous song: %s", playlist[current_song]);
                            break;

                        case 4: // 미디어 볼륨 UP
                            increase_volume();
                            snprintf(response, BUFFER_SIZE - 1, "Volume increased");
                            break;

                        case 5: // 미디어 볼륨 DOWN
                            decrease_volume();
                            snprintf(response, BUFFER_SIZE - 1, "Volume decreased");
                            break;

                        default:
                            snprintf(response, BUFFER_SIZE - 1, "Unknown command received: %d %d", category, cmd);
                            break;
                    }
                    break;  // case 4 종료 후 나가기

                case 9:
                    printf("Mode Answer!\n");
                    break;

                default:
                    snprintf(response, BUFFER_SIZE - 1, "Unknown command category received: %d %d", category, cmd);
                    break;
            }
            pthread_mutex_unlock(&audio_mutex);
        }

        if (send(cli->sock, response, strlen(response), 0) == -1) {
            perror("Failed to send response to client");
        }
    }
    close(cli->sock);
    free(cli);
    return NULL;
}

void *check_tah_file_thread(void *arg) {
    const char *file_path = (const char *)arg;
    FILE *file;
    static int last_data = -1;
    int current_data;

    while (1) {
        file = fopen(file_path, "r");
        if (file == NULL) {
            perror("Error opening file");
            sleep(1);  // Sleep for a second before retrying
            continue;
        }

        // Read the file content (you can modify this based on your needs)
        if(fscanf(file, "%d", &current_data) !=1 ){
            perror("File read error...");
            fclose(file);
            sleep(1);
            continue;
        }

        if (current_data != last_data) {
            printf("Temperature/Humidity state: %d\n", current_data);
            last_data = current_data;  // 마지막 데이터를 업데이트
        }

        fclose(file);
        sleep(1);  // Sleep for 1 second before checking again
    }

    return NULL;
}

void *check_light_file_thread(void *arg) {
    const char *file_path = (const char *)arg;
    FILE *file;
    static int last_data = -1;
    int current_data;

    while (1) {
        file = fopen(file_path, "r");
        if (file == NULL) {
            perror("Error opening file");
            sleep(1);  // Sleep for a second before retrying
            continue;
        }

        // Read the file content (you can modify this based on your needs)
        if(fscanf(file, "%d", &current_data) !=1 ){
            perror("File read error...");
            fclose(file);
            sleep(1);
            continue;
        }

        if (current_data != last_data) {
            printf("Light state: %d\n", current_data);
            last_data = current_data;  // 마지막 데이터를 업데이트
        }

        fclose(file);
        sleep(1);  // Sleep for 1 second before checking again
    }

    return NULL;
}

int main() {
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t audio_thread;
    pthread_t tah_thread;
    pthread_t light_thread;

    const char *tah_file = "./tah";
    const char *light_file = "./light";
    // const char *tah_file = "./tmp/dht11.log";
    // const char *light_file = "/tmp/light.log";

    if (initialize_mixer() < 0) {
        return -1;
    }

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Binding failed");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 5) == -1) {
        perror("Listening failed");
        close(server_socket);
        exit(1);
    }

    pthread_create(&audio_thread, NULL, audio_playback_thread, NULL);
    pthread_create(&tah_thread, NULL, check_tah_file_thread, (void *)tah_file);
    pthread_create(&light_thread, NULL, check_light_file_thread, (void *)light_file);

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Client connection failed");
            continue;
        }

        client_info *cli = (client_info *)malloc(sizeof(client_info));
        cli->sock = client_socket;
        cli->addr = client_addr;

        pthread_t client_thread;
        if(strcmp(inet_ntoa(client_addr.sin_addr), "10.42.0.1") == 0) {
            printf("Connected to Client D3\n");
            pthread_create(&client_thread, NULL, handle_bidirectional, (void *)cli);
        }
        else if (strcmp(inet_ntoa(client_addr.sin_addr), "10.42.0.4") == 0) {
            printf("Connected to External D3\n");
            pthread_create(&client_thread, NULL, handle_unidirectional, (void *)cli);
        }else {
            printf("Unknown client connected\n");
            close(client_socket);
            free(cli);
            continue;
        }
        pthread_detach(client_thread);
    }

    close(server_socket);
    pthread_join(audio_thread, NULL);

    return 0;
}
