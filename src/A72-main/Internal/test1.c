#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <mpg123.h>
#include <alsa/asoundlib.h>
#include <sys/socket.h>   
#include <sys/select.h>   

#include "mixer_control.h"
#include "internal_control.h"

#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

snd_pcm_t *initialize_alsa(const char *device, long rate, int channels);
void play_mp3(void *arg);
void play_sound(const char *filename);
void *audio_playback_thread(void *arg);
void external_check_and_update_state(int motor_status, int f_distance_sensor, int b_distance_sensor);
void *handle_unidirectional(void *arg);
void *handle_bidirectional(void *arg);
void *check_tah_file_thread(void *arg);
void *check_light_file_thread(void *arg);
void light_control();
void tah_control();

typedef struct {
    int motor_status;
    int fan_motor;
    int f_distance_sensor;
    int b_distance_sensor;
    int light_sensor;
    int tah_sensor;
} state_info;

state_info global_state = { .motor_status = -1, .fan_motor = 0, .f_distance_sensor = 0, .b_distance_sensor = 0, .light_sensor=0, .tah_sensor=0 };

typedef struct {
    int sock;
    struct sockaddr_in addr;
} client_info;

// Playlist variables
const char *playlist[] = {
    "./music/song1.mp3",
    "./music/song2.mp3",
    "./music/song3.mp3"
};

const char *comment[] = {
    "./music/start.mp3",
    "./music/receive_command.mp3",
    "./music/light_on.mp3",
    "./music/light_off.mp3",
    "./music/front_danger.mp3",
    "./music/back_danger.mp3",
    "./music/fan_on.mp3",
    "./music/fan_off.mp3",
    "./music/alert.mp3",
    "./music/wrong_command.mp3",
    "./music/server_error.mp3",
    "./music/timeout.mp3",
    "./music/unknown_error.mp3",
    "./music/already_not_work_fan.mp3",
    "./music/already_work_fan.mp3",
    "./music/already_not_work_led.mp3",
    "./music/already_work_led.mp3",
    "./music/already_auto_mode.mp3",  
    "./music/change_to_auto_mode.mp3",
    "./music/change_to_manual_mode.mp3",
    "./music/already_manual_mode.mp3",
    "./music/unknown_command.mp3",
    "./music/stop_by_back.mp3",
    "./music/start_system.mp3",
};

pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t comment_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t external_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t internal_mutex = PTHREAD_MUTEX_INITIALIZER;

mpg123_handle *mpg_handle = NULL;
snd_pcm_t *playback_handle = NULL;

volatile int current_song = 0;
volatile int is_paused = 1;
volatile int song_change_request = 0;
volatile int stop_request = 0;
volatile int is_playing_music = 0;

volatile int light_cmd = 0;
volatile int tah_cmd = 0;

volatile int mode_flag = 1;
volatile int tah_flag = 1;
volatile int light_flag = 1;

volatile int is_comment = 0;
pthread_t audio_thread;
static off_t saved_frame = 0;  // 저장된 타임라인 (프레임)

int num_files = sizeof(playlist) / sizeof(playlist[0]);


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

void play_mp3(void *arg) {
    const char *filename = (const char *)arg;
    mpg123_handle *mpg_handle;
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

    // 프레임으로 이동 (saved_frame에서 재생 시작)
    if (saved_frame > 0) {
        printf("Resuming playback from frame: %ld\n", saved_frame);
        mpg123_seek_frame(mpg_handle, saved_frame, SEEK_SET);
    } else {
        printf("Starting playback from the beginning\n");
    }

    mpg123_getformat(mpg_handle, &rate, &channels, &encoding);

    if (playback_handle != NULL)
    {
        snd_pcm_drain(playback_handle);
        snd_pcm_close(playback_handle);
        printf("clear mp3 handler\n");
        playback_handle = NULL;
    }

    playback_handle = initialize_alsa("plughw:1,0", rate, channels);
    if (!playback_handle) {
        mpg123_close(mpg_handle);
        mpg123_delete(mpg_handle);
        mpg123_exit();
        return;
    }

    buffer_size = mpg123_outblock(mpg_handle);
    buffer = (unsigned char *)malloc(buffer_size);
    is_playing_music = 1;
    while (1) {
        pthread_mutex_lock(&audio_mutex);
        if (stop_request) {
            saved_frame = mpg123_tellframe(mpg_handle);
            if (playback_handle != NULL)
            {
                printf("Closing playback_handle\n");
                snd_pcm_drop(playback_handle);
                snd_pcm_close(playback_handle);
                playback_handle = NULL;
            }
            pthread_mutex_unlock(&audio_mutex);
            break;
        }

        if (mpg123_read(mpg_handle, buffer, buffer_size, &done) != MPG123_OK) {
            break;
        }
        
        err = snd_pcm_writei(playback_handle, buffer, done / (2 * channels));
        if (err == -EPIPE) {
            fprintf(stderr, "Buffer underrun occurred\n");
            snd_pcm_prepare(playback_handle);
        } else if (err < 0) {
            fprintf(stderr, "Playback error: %s\n", snd_strerror(err));
            break;
        }
        pthread_mutex_unlock(&audio_mutex);
    }
    pthread_mutex_lock(&audio_mutex);
    if (buffer)
    {
        free(buffer);
    }
    
    stop_request = 0;
    is_playing_music = 0;
    pthread_mutex_unlock(&audio_mutex);
    mpg123_close(mpg_handle);
    mpg123_delete(mpg_handle);
    mpg123_exit();
}

void play_sound(const char *filename) {
    mpg123_handle *mpg_handle;
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

    if (playback_handle != NULL)
    {
        snd_pcm_drain(playback_handle);
        snd_pcm_close(playback_handle);
        printf("clear sound handler\n");
        playback_handle = NULL;
    }

    playback_handle = initialize_alsa("plughw:1,0", rate, channels);
    if (!playback_handle) {
        mpg123_close(mpg_handle);
        mpg123_delete(mpg_handle);
        mpg123_exit();
        return;
    }

    buffer_size = mpg123_outblock(mpg_handle);
    buffer = (unsigned char *)malloc(buffer_size);

    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for buffer\n");
        snd_pcm_close(playback_handle);
        mpg123_close(mpg_handle);
        mpg123_delete(mpg_handle);
        mpg123_exit();
        return;
    }

    while (1) {

        if (mpg123_read(mpg_handle, buffer, buffer_size, &done) != MPG123_OK) {
            break;
        }
        pthread_mutex_lock(&comment_mutex);
        err = snd_pcm_writei(playback_handle, buffer, done / (2 * channels));
        if (err == -EPIPE) {
            fprintf(stderr, "Buffer underrun occurred\n");
            snd_pcm_prepare(playback_handle);
        } else if (err < 0) {
            fprintf(stderr, "Playback error: %s\n", snd_strerror(err));
            break;
        }
        pthread_mutex_unlock(&comment_mutex);

    }

    free(buffer);
    if (playback_handle != NULL)
    {
        snd_pcm_drain(playback_handle);
        snd_pcm_close(playback_handle);
    }
    
    playback_handle = NULL;
    mpg123_close(mpg_handle);
    mpg123_delete(mpg_handle);
    mpg123_exit();
    is_comment = 0;
}

void *audio_playback_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&audio_mutex);
        int song_index = current_song;
        song_change_request = 0;
        pthread_mutex_unlock(&audio_mutex);

        while(is_comment){
            usleep(100000);
        }

        // play_mp3(playlist[song_index]);

        pthread_mutex_lock(&audio_mutex);
        if (!song_change_request) {
            current_song = (current_song + 1) % num_files;
        }
        pthread_mutex_unlock(&audio_mutex);
    }
    return NULL;
}

void external_check_and_update_state(int motor_status, int f_distance_sensor, int b_distance_sensor) {
    pthread_mutex_lock(&external_mutex); 
    if ( 
            (   (global_state.motor_status != motor_status) ||
                (global_state.f_distance_sensor != f_distance_sensor) || 
                (global_state.b_distance_sensor != b_distance_sensor) 
            ) 
            &&
            (
                (motor_status==2 && b_distance_sensor > global_state.b_distance_sensor) || (motor_status ==4 && b_distance_sensor==3) ||
                (motor_status==1 && f_distance_sensor > global_state.f_distance_sensor)
            )

        ) 
    {
        printf("%d %d %d\n", motor_status, f_distance_sensor, b_distance_sensor);
        
        if(motor_status==4){
            // printf("Danger Break\n"); 
            // comment[22]
            is_comment = 1;
            play_sound(comment[8]);
            is_comment = 1;
            play_sound(comment[22]);
        }
        else if(b_distance_sensor==3){
            // printf("Back Danger\n");
            // comment[8]
            is_comment = 1;
            play_sound(comment[8]);
        }
        else if(b_distance_sensor==2){
            // printf("Back Object Detection\n");
            // comment[5]
            is_comment = 1;
            play_sound(comment[5]);
        }
        else if(f_distance_sensor==3){
            // printf("Front Danger\n");
            // comment[8]
            is_comment = 1;
            play_sound(comment[8]);
        }
        else if(f_distance_sensor==2){
            // printf("Front Object Detection\n");
            // comment[4]
            is_comment = 1;
            play_sound(comment[4]);
        }

        // 전역 상태 업데이트
        global_state.motor_status = motor_status;
        global_state.f_distance_sensor = f_distance_sensor;
        global_state.b_distance_sensor = b_distance_sensor;

    }
    pthread_mutex_unlock(&external_mutex); // 상태 접근 해제
}

void *handle_unidirectional(void *arg) {
    client_info *cli = (client_info *)arg;
    char buffer[BUFFER_SIZE];
    int motor_status, f_distance_sensor, b_distance_sensor;

    while (1) {
        int recv_len = recv(cli->sock, buffer, BUFFER_SIZE - 1, 0);
        if (recv_len <= 0) {
            printf("External D3 disconnected\n");
            break;
        }
        buffer[recv_len] = '\0';
        printf("Received from External D3: %s\n", buffer);

        if (sscanf(buffer, "%d %d %d", &motor_status, &f_distance_sensor, &b_distance_sensor) == 3) {
            external_check_and_update_state(motor_status, f_distance_sensor, b_distance_sensor); // 상태 변화 확인 및 출력
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
            snprintf(response, BUFFER_SIZE - 1, "-1 -1");
            if (send(cli->sock, response, strlen(response), 0) == -1) {
                perror("Failed to send response");
                printf("Server Send Error 1\n");
            }
            // comment[9]
            is_comment = 1;
            play_sound(comment[9]);
            continue;
        }

        if (category != 1 || cmd !=0) {
            printf("Initial command should be CALL\n");
            snprintf(response, BUFFER_SIZE - 1, "-1 -1");
            if (send(cli->sock, response, strlen(response), 0) == -1) {
                perror("Failed to send invalid category response");
                printf("Server Send Error 2\n");
            }
            is_comment = 1;
            play_sound(comment[9]);
            continue;
        }

        snprintf(response, BUFFER_SIZE - 1, "1 0");
        if (send(cli->sock, response, strlen(response), 0) == -1) {
            perror("Failed to send acknowledgment");
            printf("Server Send Error 3\n");
            // comment[10]
            is_comment = 1;
            play_sound(comment[10]);
            continue;
        }

        if (is_playing_music)
        {
            stop_request = 1;
            if (pthread_join(audio_thread, NULL) != 0) {
                fprintf(stderr, "Failed to join audio thread\n");
                return -1;
            }
            
        }
        // + comment[1]
        
        is_comment = 1;
        play_sound(comment[1]);
        // 10초간 응답 대기
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(cli->sock, &read_fds);
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        int activity = select(cli->sock + 1, &read_fds, NULL, NULL, &timeout);
        if (activity == -1) {
            perror("select error");
            printf("Select Error\n");

            snprintf(response, BUFFER_SIZE - 1, "-1 -1");
            if (send(cli->sock, response, strlen(response), 0) == -1) {
                perror("Failed to send error response");
                printf("Server Send Error 4\n");
            }
            // comment[10]
            is_comment = 1;
            play_sound(comment[10]);
            continue;
        } 
        else if (activity == 0) {
            printf("Timeout: No response from client\n");
            snprintf(response, BUFFER_SIZE - 1, "-1 -1");
            if (send(cli->sock, response, strlen(response), 0) == -1) {
                perror("Failed to send timeout response");
                printf("Server Send Error 5\n");
            }
            // comment[11]
            is_comment = 1;
            play_sound(comment[11]);
            continue;
        } 
        else if (FD_ISSET(cli->sock, &read_fds)) {
            recv_len = recv(cli->sock, buffer, BUFFER_SIZE - 1, 0);
            if (recv_len > 0) {
                buffer[recv_len] = '\0';
                if (sscanf(buffer, "%d %d", &category, &cmd) < 2) {
                    snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                    if (send(cli->sock, response, strlen(response), 0) == -1) {
                        perror("Failed to send invalid command response");
                        printf("Server Send Error 6\n");
                    }
                    // comment[9]
                    is_comment = 1;
                    play_sound(comment[9]);
                    continue;
                }
            } 
            else {
                perror("Error during command reception");
                printf("Error during command reception\n");
                snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                if (send(cli->sock, response, strlen(response), 0) == -1) {
                    perror("Failed to send error response");
                    printf("Server Send Error 7\n");
                }
                // comment[12]
                is_comment = 1;
                play_sound(comment[12]);
                continue;
            }
        }

        switch (category) {
            case 2:
                switch (cmd) {
                    case 0: // Fan Motor OFF
                        snprintf(response, BUFFER_SIZE - 1, "2 0");
                        if(tah_cmd != 0){
                            printf("Stop Fan Motor Now\n");

                            tah_flag = 1;
                            tah_cmd = 0;
                            pthread_mutex_lock(&internal_mutex);
                            tah_control();
                            pthread_mutex_unlock(&internal_mutex);
                        }
                        else{
                            printf("Fan Motor Already doesn't work!\n");
                           
                            // comment[13]  
                            is_comment = 1;
                            play_sound(comment[13]);
                           
                        }
                        break;

                    case 1: // Fan Motor ON
                        snprintf(response, BUFFER_SIZE - 1, "2 1");
                        if(tah_cmd != 1){ 
                            printf("Fan Motor ON START!\n");

                            tah_flag = 1;
                            tah_cmd = 1;
                            pthread_mutex_lock(&internal_mutex);
                            tah_control();
                            pthread_mutex_unlock(&internal_mutex);
                        }
                        else{
                            printf("Fan Motor Already works!\n");

                            // comment[14] 
                            is_comment = 1;
                            play_sound(comment[14]);
                            
                        }
                        break;
                        
                    default:
                        snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                        printf("Unknown Fan Motor CMD\n");
                        is_comment = 1;
                        play_sound(comment[21]);
                        break;
                }
                break;

            case 3:
                switch (cmd) {
                    case 0: // Light OFF
                        snprintf(response, BUFFER_SIZE - 1, "3 0");

                        if(light_cmd != 0){ 
                            printf("Stop LED Now\n");
                            
                            light_flag = 1;
                            light_cmd = 0;
                            pthread_mutex_lock(&internal_mutex);
                            light_control();
                            pthread_mutex_unlock(&internal_mutex);
                        }else{
                            printf("Light Already doesn't works!\n");
                            
                            // comment[15] 
                            is_comment = 1;
                            play_sound(comment[15]);

                        }
                        break;
                    case 1: // Light ON
                        snprintf(response, BUFFER_SIZE - 1, "3 1");
                        
                        if(light_cmd != 1){ 
                            printf("LED ON START\n");

                            light_flag = 1;
                            light_cmd = 1;
                            pthread_mutex_lock(&internal_mutex);
                            light_control();
                            pthread_mutex_unlock(&internal_mutex);
                        }else{
                            printf("Already LED ON\n");
                            // comment[16]
                            is_comment = 1;
                            play_sound(comment[16]);
                        }
                        break;
                    default:
                        snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                        printf("Unknown Light CMD\n");
                        is_comment = 1;
                        play_sound(comment[21]);
                        break;
                }
                break;

            case 4:
                switch (cmd) {
                    case 0: // 일시정지
                        is_paused = 1;
                        snprintf(response, BUFFER_SIZE - 1, "4 0");
                        stop_request = 1;
                        if (pthread_join(audio_thread, NULL) != 0) {
                            fprintf(stderr, "Failed to join audio thread\n");
                            return -1;
                        }
                        printf("Playback is now paused\n");
                        break;
                    case 1: // 재생
                        is_paused = 0;
                        snprintf(response, BUFFER_SIZE - 1, "4 1");
                        pthread_create(&audio_thread, NULL, play_mp3, playlist[current_song]);
                        printf("Playback is now resumed\n");
                        break;

                    case 2: // 다음 곡
                        current_song = (current_song + 1) % num_files;
                        song_change_request = 1;
                        is_paused = 0;
                        snprintf(response, BUFFER_SIZE - 1, "4 2");
                        stop_request = 1;
                        if (pthread_join(audio_thread, NULL) != 0) {
                            fprintf(stderr, "Failed to join audio thread\n");
                            return -1;
                        }
                        saved_frame = 0;
                        pthread_create(&audio_thread, NULL, play_mp3, playlist[current_song]);
                        printf("Playing next song: %s\n", playlist[current_song]);

                        break;

                    case 3: // 이전 곡
                        current_song = (current_song - 1 + num_files) % num_files;
                        song_change_request = 1;
                        is_paused = 0;
                        snprintf(response, BUFFER_SIZE - 1, "4 3");
                        stop_request = 1;
                        if (pthread_join(audio_thread, NULL) != 0) {
                            fprintf(stderr, "Failed to join audio thread\n");
                            return -1;
                        }
                        saved_frame = 0;
                        pthread_create(&audio_thread, NULL, play_mp3, playlist[current_song]);
                        printf("Playing previous song: %s\n", playlist[current_song]);
                        break;

                    case 4: // 미디어 볼륨 UP
                        increase_volume();
                        snprintf(response, BUFFER_SIZE - 1, "4 4");
                        printf("Volume increased\n");
                        break;

                    case 5: // 미디어 볼륨 DOWN
                        decrease_volume();
                        snprintf(response, BUFFER_SIZE - 1, "4 5");
                        printf("Volume decreased\n");
                        break;

                    default:
                        snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                        printf("Unknown Media CMD\n");
                        is_comment = 1;
                        play_sound(comment[21]);
                        break;
                }
                break;

            case 9:
                switch (cmd) {
                    case 0: // Auto Mode
                        snprintf(response, BUFFER_SIZE - 1, "9 0");

                        if(mode_flag==0){
                            printf("Already Auto Mode\n");

                            // comment[17]
                            is_comment = 1;
                            play_sound(comment[17]);
                        }else if(mode_flag==1){
                            printf("Change to Auto Mode\n");

                            // comment[18]
                            is_comment = 1;
                            play_sound(comment[18]);

                            pthread_mutex_lock(&internal_mutex);
                            mode_flag = 0;
                            tah_flag =  0;
                            light_flag = 0;
                            pthread_mutex_unlock(&internal_mutex);
                        }
                        break;

                    case 1: // Manual Mode
                        snprintf(response, BUFFER_SIZE - 1, "9 1");
                        
                        if(mode_flag==0){
                            printf("Change to Manual Mode\n");
                            // comment[19]
                            is_comment = 1;
                            play_sound(comment[19]);

                            pthread_mutex_lock(&internal_mutex);
                            mode_flag = 1;
                            tah_flag =  1;
                            light_flag = 1;
                            pthread_mutex_unlock(&internal_mutex);
                        }else if(mode_flag==1){
                            printf("Already Manual Mode\n");
                            // comment[20]
                            is_comment = 1;
                            play_sound(comment[20]);
                        }
                        break;

                    default:
                        snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                        printf("Unknown Mode Change CMD\n");
                        is_comment = 1;
                        play_sound(comment[21]);
                        break;
                }
                break;

            default:
                printf("Unknown CMD\n");
                // comment[21]
                is_comment = 1;
                play_sound(comment[21]);
                snprintf(response, BUFFER_SIZE - 1, "-1 -1");
                break;
        }

        if (send(cli->sock, response, strlen(response), 0) == -1) {
            perror("Failed to send response");
            printf("Server Send Error 8\n");
        }
    }

    close(cli->sock);
    free(cli);
    return NULL;
}

void light_control() {
    if(light_cmd==0){
        printf("Enter the Light OFF Function!\n");
        is_comment = 1;
        play_sound(comment[3]);
        turn_off_light();
    }

    if(light_cmd==1){
        printf("Enter the Light ON Function! \n");
        is_comment = 1;
        play_sound(comment[2]);
        turn_on_light();
    }
}

void tah_control() {
    if(tah_cmd==0){
        printf("Enter the Tah OFF Function!\n");
        is_comment = 1;
        play_sound(comment[7]);
        turn_off_fan();
    }

    if(tah_cmd==1){
        printf("Enter the Tah ON Function!\n");
        is_comment = 1;
        play_sound(comment[6]);
        turn_on_fan();

    }
}

void *check_tah_file_thread(void *arg) {
    const char *file_path = (const char *)arg;
    FILE *file;
    static int last_data = 0;
    int current_data;

    while (1) {
        file = fopen(file_path, "r");
        if (file == NULL) {
            perror("Error opening file");
            sleep(1); 
            continue;
        }

        // Read file c
        if(fscanf(file, "%d", &current_data) !=1 ){
            perror("File read error...");
            fclose(file);
            sleep(1);
            continue;
        }

        if (current_data != last_data && mode_flag==0) {
            printf("Temperature/Humidity state: %d\n", current_data);
            last_data = current_data;  // 마지막 데이터 업데이트
            if(tah_flag == 0){
                tah_cmd = current_data;
                pthread_mutex_lock(&internal_mutex);
                tah_control();
                pthread_mutex_unlock(&internal_mutex);
            }
        }

        fclose(file);
        sleep(1);  
    }
}

void *check_light_file_thread(void *arg) {
    const char *file_path = (const char *)arg;
    FILE *file;
    static int last_data = 0;
    int current_data;

    while (1) {
        file = fopen(file_path, "r");
        if (file == NULL) {
            perror("Error opening file");
            sleep(1); 
            continue;
        }

        // Read file 
        if(fscanf(file, "%d", &current_data) !=1 ){
            perror("File read error...");
            fclose(file);
            sleep(1);
            continue;
        }

        if (current_data != last_data && mode_flag==0) {
            printf("Light state: %d\n", current_data);
            last_data = current_data;  // 마지막 데이터 업데이트
            if(light_flag == 0){
                light_cmd = current_data;
                pthread_mutex_lock(&internal_mutex);
                light_control();
                pthread_mutex_unlock(&internal_mutex);
            }
        }

        fclose(file);
        sleep(1);  
    }
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t audio_thread;
    pthread_t tah_file_thread;
    pthread_t light_file_thread;

    // const char *tah_file = "./tah";
    // const char *light_file = "./light";
    const char *tah_file = "/tmp/dht11.log";
    const char *light_file = "/tmp/light.log";
    int server_socket, client_socket;
    int button_state = 0;
    int set_mode = 0;

    char buffer[1024];
    char response[1024];
    int mode_set = 0;

    if (initialize_mixer() < 0) {
        return -1;
    }

    export_gpio(GPIO_113);
    set_gpio_direction_in(GPIO_113);

    while (1) {
        printf("checkpoint1 \n");
        if (get_gpio_value(GPIO_113)) {

            // comment[23]
            is_comment = 1;
            play_sound(comment[23]);

            break;
        }
        usleep(100000);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    // pthread_create(&audio_thread, NULL, audio_playback_thread, NULL);
    pthread_create(&tah_file_thread, NULL, check_tah_file_thread, (void *)tah_file);
    pthread_create(&light_file_thread, NULL, check_light_file_thread, (void *)light_file);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Binding failed");
        close(server_socket);
    }

    if (listen(server_socket, 5) == -1) {
        perror("Listening failed");
        close(server_socket);
    }

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("Client connection failed");
            continue;
        }

        if(!set_mode){
            is_comment = 1;
            play_sound(comment[0]);

            sleep(3);

            is_comment = 1;
            play_sound(comment[18]);
            mode_flag=0;
            light_flag = 0;
            tah_flag = 0;
            set_mode = 1;
        }
        


        // while (!set_mode) {
        //     //comment[0]
        //     is_comment = 1;
        //     play_sound(comment[0]);

        //     // Send mode request to the client
        //     snprintf(buffer, sizeof(buffer), "9 2");
        //     if (send(client_socket, buffer, strlen(buffer), 0) == -1) {
        //         perror("Failed to send mode request");
        //         close(client_socket);
        //         close(server_socket);
        //         continue;
        //     }

        //     fd_set readfds;
        //     struct timeval timeout;
        //     int tmp;
        //     FD_ZERO(&readfds);
        //     FD_SET(client_socket, &readfds);

        //     timeout.tv_sec = 7; 
        //     timeout.tv_usec = 0;

        //     int activity = select(client_socket + 1, &readfds, NULL, NULL, &timeout);

        //     if (activity == -1) {
        //         perror("select error");
        //         printf("Select Error\n");
        //         snprintf(response, BUFFER_SIZE - 1, "-1 -1");
 
        //         // comment[10]
        //         is_comment = 1;
        //         play_sound(comment[10]);
        //         continue;
        //     } 
        //     else if (activity == 0) {
        //         printf("No response from client\n");
 
        //         // comment[11]
        //         is_comment = 1;
        //         play_sound(comment[11]);
        //         continue;
        //     } 
        //     else if (FD_ISSET(client_socket, &readfds)) {
        //         int recv_len = recv(client_socket, response, sizeof(response) - 1, 0);
        //         if (recv_len > 0) {
        //             response[recv_len] = '\0';
        //             printf("Received response from client: %s\n", response);

        //             if (sscanf(response, "%d %d", &tmp, &mode_flag) < 2 || tmp !=9) {
        //                 // comment[9]
        //                 is_comment = 1;
        //                 play_sound(comment[9]);
        //                 continue;
        //             }

        //             if(mode_flag==1){
        //                 is_comment = 1;
        //                 play_sound(comment[19]);
        //                 light_flag = 1;
        //                 tah_flag = 1;
        //             }else if(mode_flag==0){
        //                 is_comment = 1;
        //                 play_sound(comment[18]);
        //                 light_flag = 0;
        //                 tah_flag = 0;
        //             }else{

        //                 is_comment = 1;
        //                 play_sound(comment[9]);
        //                 continue;
        //             }
        //             set_mode = 1;  // Exit loop on valid response

        //         } else {
        //             printf("Error or disconnection during mode configuration.\n");
        //             // comment[9]
        //             is_comment = 1;
        //             play_sound(comment[9]);
        //             continue;
        //         }
        //     }
        // }

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
    pthread_join(light_file_thread, NULL);
    pthread_join(tah_file_thread, NULL);

    // 종료 시 GPIO 해제
    unexport_gpio(GPIO_113);

    return 0;
}
