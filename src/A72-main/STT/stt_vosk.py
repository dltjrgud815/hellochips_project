import wave
import sys
import json
from vosk import Model, KaldiRecognizer
from pydub import AudioSegment
import os
from scipy.signal import butter, lfilter
import numpy as np
import time
import webrtcvad
import alsaaudio
import threading
import noisereduce as nr
from rapidfuzz import fuzz, process

SERVER_IP = "10.42.0.2"
SERVER_PORT = 12345
BUFFER_SIZE = 1024
isProcess = False

# commands = ["헬로 칩스","등 켜줘", "등 꺼줘", "에어컨 켜줘", "에어컨 꺼줘",
#              "음악 재생해줘","음악 틀어줘", 
#              "음악 멈춰줘", "음악 꺼줘",
#              "다음곡으로 넘겨줘", "다음곡 재생",
#              "이전곡으로 넘겨줘", "이전곡 재생",
#              "음량 키워줘", "볼륨 높여줘",
#              "음량 낮춰줘", "볼륨 줄여줘"]

# 명령어와 번호 매핑
commands = {
    "헬로 칩스": "1 0",
    "에어컨 켜줘": "2 1",
    "에어컨 꺼 줘": "2 0",
    "등 켜줘": "3 1",
    "등 꺼 줘": "3 0",
    "음악 재생 해줘": "4 1", "음악 틀어 줘": "4 1",
    "음악 멈춰 줘": "4 1", "음악 꺼 줘": "4 1",
    "다음 곡 재생 해줘": "4 2", "다음 곡 재생": "4 2",
    "이전 곡 재생 해줘": "4 3", "이전 곡 재생": "4 3",
    "음량 키워 줘": "4 4", "볼륨 높여 줘": "4 4",
    "음량 낮춰 줘": "4 5", "볼륨 줄여 줘": "4 5"
}

def receive_data(client_socket):
    """
    서버로부터 데이터를 수신하는 함수 (별도 스레드에서 실행)
    """
    while True:
        try:
            data = client_socket.recv(BUFFER_SIZE)
            if not data:
                print("서버와의 연결이 종료되었습니다.")
                break
            print(f"[서버]: {data.decode('utf-8')}")
        except Exception as e:
            print(f"데이터 수신 중 오류 발생: {e}")
            break
    client_socket.close()

def convert_mp3_to_wav(mp3_file, wav_file):
    """
    MP3 파일을 Vosk가 지원하는 WAV 형식으로 변환합니다.
    
    Parameters:
        mp3_file (str): 입력 MP3 파일 경로.
        wav_file (str): 출력 WAV 파일 경로.
    """
    # MP3 파일 열기
    audio = AudioSegment.from_file(mp3_file, format="mp3")
    
    # Vosk 요구 사항에 맞게 변환: 16kHz, 16-bit, mono
    audio = audio.set_frame_rate(16000)  # 16kHz 샘플링
    audio = audio.set_channels(1)       # 모노
    audio = audio.set_sample_width(2)   # 16-bit

    # WAV 파일로 저장
    audio.export(wav_file, format="wav")
    print(f"Converted {mp3_file} to {wav_file}")


def save_as_mp3(audio_data, filename, sample_rate):
    """Pydub을 사용해 MP3로 저장"""
    audio_segment = AudioSegment(
        audio_data.astype(np.int16).tobytes(),
        frame_rate=sample_rate,
        sample_width=2,  # 16-bit PCM
        channels=1  # 모노
    )
    audio_segment.export(filename, format="mp3")

def butter_bandpass(lowcut, highcut, fs, order=5):
    """대역 통과 필터 생성"""
    nyquist = 0.5 * fs
    low = lowcut / nyquist
    high = highcut / nyquist
    b, a = butter(order, [low, high], btype='band')
    return b, a

def bandpass_filter(data, lowcut=300.0, highcut=3000.0, fs=16000, order=5):
    """대역 통과 필터를 적용하여 소음 제거"""
    b, a = butter_bandpass(lowcut, highcut, fs, order=order)
    return lfilter(b, a, data)

def reduce_noise(filename):
    """녹음된 파일에서 노이즈 제거 후 새로운 파일로 저장"""
    with wave.open(filename, "rb") as f:
        sample_rate = f.getframerate()
        channels = f.getnchannels()
        audio_data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    
    # 노이즈 제거 수행
    reduced_noise = nr.reduce_noise(y=audio_data, sr=sample_rate)
    noise_reduced_filename = "noise_reduced_" + filename
    # with wave.open(noise_reduced_filename, "wb") as f:
    #     f.setnchannels(channels)
    #     f.setsampwidth(2)
    #     f.setframerate(sample_rate)
    #     f.writeframes(reduced_noise.astype(np.int16).tobytes())

    audio_segment = AudioSegment(
        reduced_noise.astype(np.int16).tobytes(),
        frame_rate=sample_rate,
        sample_width=2,  # 16-bit (2 bytes)
        channels=1  # 모노
    )
    audio_segment.export(noise_reduced_filename, format="wav" , codec="pcm_s16le")    
    print("노이즈 제거 완료:", noise_reduced_filename)
    return noise_reduced_filename


def save_audio_to_file(audio_data, sample_rate, output_file):
    """오디오 데이터를 파일로 저장"""
    with wave.open(output_file, "wb") as wf:
        wf.setnchannels(1)  # 모노 채널
        wf.setsampwidth(2)  # 16비트 (2바이트)
        wf.setframerate(sample_rate)
        wf.writeframes(audio_data.astype(np.int16).tobytes())
    print(f"오디오 데이터가 {output_file}에 저장되었습니다.")

def vad_for_hellochips(sample_rate=16000, vad_sensitivity=3, frame_duration=20):
    """VAD를 사용하여 음성을 녹음하고 자동 종료"""
    inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE, alsaaudio.PCM_NORMAL, device="plughw:1,0")
    inp.setchannels(1)
    inp.setrate(sample_rate)
    inp.setformat(alsaaudio.PCM_FORMAT_S16_LE)
    inp.setperiodsize(1024)

    vad = webrtcvad.Vad()
    vad.set_mode(vad_sensitivity)  # VAD 민감도 설정 (0~3)

    audio_data = []
    print("1초 이상의 음성 감지 중")

    #sysf.control_rgb_led_async()

    silence_start_time = time.time()
    speech_start_time = None
    max_silence_duration = 1.0  # 1초 동안 음성 없음 감지 시 종료
    frame_size = int(sample_rate * (frame_duration / 1000)) * 2  # 20ms 프레임 크기 계산
    min_speech_duration = 1.0  # 최소 1초 이상 음성이 감지될 때만 저장
    output_file = "post_vad.wav"

    while True:
        length, data = inp.read()
        if length > 0:
            audio_chunk = np.frombuffer(data, dtype=np.int16)

            # 필터 적용: 프레임 단위로 처리
            filtered_chunk = bandpass_filter(audio_chunk, lowcut=300.0, highcut=3000.0, fs=sample_rate)
            
            # VAD와 연동
            if len(filtered_chunk) * 2 >= frame_size:  # 프레임 크기가 충분히 클 경우만 처리
                frame = filtered_chunk[:frame_size // 2].astype(np.int16)  # 프레임 크기 맞추기
                #try:
                is_speech = vad.is_speech(frame.tobytes(), sample_rate)
                # except webrtcvad.Error as e:
                #     print(f"VAD 처리 오류: {e}")
                #     break

                # 디버깅: 음성 감지 여부와 타이머 상태 출력
                if is_speech:
                    print("[디버깅] 음성 감지됨!")
                    silence_start_time = time.time()  # 음성 감지 시 타이머 초기화
                    if speech_start_time is None:
                       speech_start_time = time.time()
                    audio_data.append(audio_chunk)
                else:
                    if silence_start_time is None:
                        silence_start_time = time.time()  # 음성 감지가 중지된 시점 기록

                    elapsed_silence = time.time() - silence_start_time if silence_start_time else 0
                    print(f"[디버깅] 음성 감지되지 않음 (타이머: {elapsed_silence:.2f}s)")

                    # 음성이 1초 이상 감지된 후, 1초 동안 음성이 감지되지 않은 경우 저장
                    if speech_start_time and (time.time() - speech_start_time >= min_speech_duration) and elapsed_silence >= max_silence_duration:
                        print("음성 녹음을 저장합니다.")
                        audio_data = np.concatenate(audio_data)
                        save_audio_to_file(audio_data, sample_rate, output_file)
                        isProcess = True
                        break
    
  
def record_audio_with_vad(filename, sample_rate=44100, vad_sensitivity=3, frame_duration=20):
    """VAD를 사용하여 음성을 녹음하고 자동 종료"""
    inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE, alsaaudio.PCM_NORMAL, device="plughw:1,0")
    inp.setchannels(1)
    inp.setrate(sample_rate)
    inp.setformat(alsaaudio.PCM_FORMAT_S16_LE)
    inp.setperiodsize(1024)

    vad = webrtcvad.Vad()
    vad.set_mode(vad_sensitivity)  # VAD 민감도 설정 (0~3)

    audio_data = []
    print("녹음 시작... 음성이 감지되지 않으면 자동으로 종료됩니다.")

    #sysf.control_rgb_led_async()

    silence_start_time = time.time()
    max_silence_duration = 1.0  # 1초 동안 음성 없음 감지 시 종료
    frame_size = int(sample_rate * (frame_duration / 1000)) * 2  # 20ms 프레임 크기 계산
    min_speech_duration = 1.0  # 최소 1초 이상 음성이 감지될 때만 저장

    while True:
        length, data = inp.read()
        if length > 0:
            audio_chunk = np.frombuffer(data, dtype=np.int16)

            # 필터 적용: 프레임 단위로 처리
            filtered_chunk = bandpass_filter(audio_chunk, lowcut=300.0, highcut=3000.0, fs=sample_rate)
            
            # VAD와 연동
            if len(filtered_chunk) * 2 >= frame_size:  # 프레임 크기가 충분히 클 경우만 처리
                frame = filtered_chunk[:frame_size // 2].astype(np.int16)  # 프레임 크기 맞추기
                try:
                    is_speech = vad.is_speech(frame.tobytes(), sample_rate)
                except webrtcvad.Error as e:
                    print(f"VAD 처리 오류: {e}")
                    break

                # 디버깅: 음성 감지 여부와 타이머 상태 출력
                if is_speech:
                    print("[디버깅] 음성 감지됨!")
                    silence_start_time = time.time()  # 음성 감지 시 타이머 초기화
                else:
                    elapsed_silence = time.time() - silence_start_time
                    print(f"[디버깅] 음성 감지되지 않음 (타이머: {elapsed_silence:.2f}s)")

                # 음성이 감지되지 않은 시간 체크
                if time.time() - silence_start_time > max_silence_duration:
                    print("음성이 감지되지 않아 녹음을 종료합니다.")
                    break

            # 필터링된 데이터를 저장
            audio_data.append(filtered_chunk)

    # 오디오 데이터를 결합하여 파일로 저장
    audio_data = np.concatenate(audio_data)

    # with wave.open("recorded_audio_wave.wav", "wb") as f:
    #     f.setnchannels(1)
    #     f.setsampwidth(2)
    #     f.setframerate(sample_rate)
    #     f.writeframes(audio_data.astype(np.int16).tobytes())
    # print("녹음 완료:", "recorded_audio_wave.wav")
    # return filename
     # Pydub을 사용하여 WAV 저장
    # audio_segment = AudioSegment(
    #     audio_data.astype(np.int16).tobytes(),
    #     frame_rate=sample_rate,
    #     sample_width=2,  # 16-bit (2 bytes)
    #     channels=1  # 모노
    #)
    #audio_segment.export(filename, format="wav" , codec="pcm_s16le")
    #print("녹음 완료:", filename)
    #compare_audio_saving()
    return filename

def record_audio(filename, sample_rate=16000, duration=2):
    # ALSA 설정
    inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE, alsaaudio.PCM_NORMAL, device="plughw:1,0")
    inp.setchannels(1)  # 모노
    inp.setrate(sample_rate)
    inp.setformat(alsaaudio.PCM_FORMAT_S16_LE)  # 16-bit PCM
    inp.setperiodsize(1024)

    print(f"녹음 시작... {duration}초 동안 녹음합니다.")
    
    audio_data = []
    start_time = time.time()

    while time.time() - start_time < duration:
        length, data = inp.read()
        if length > 0:
            audio_chunk = np.frombuffer(data, dtype=np.int16)
            audio_data.append(audio_chunk)

    # 녹음된 데이터를 하나의 배열로 결합
    audio_data = np.concatenate(audio_data)

    # Pydub을 사용하여 WAV 저장
    audio_segment = AudioSegment(
        audio_data.astype(np.int16).tobytes(),
        frame_rate=sample_rate,
        sample_width=2,  # 16-bit
        channels=1  # 모노
    )
    audio_segment.export(filename, format="wav", codec="pcm_s16le")
    print("녹음 완료:", filename)
    return filename

# Vosk 한국어 모델 로드
model = Model(lang="ko")

def transcribe_audio(filename):
    # 파일 존재 여부 확인
    if not os.path.exists(filename):
        print(f"파일 '{filename}'이(가) 존재하지 않습니다.")
        return

    # WAV 파일 열기
    wf = wave.open(filename, "rb")

    # WAV 파일 포맷 확인
    if wf.getnchannels() != 1 or wf.getsampwidth() != 2 or wf.getcomptype() != "NONE":
        print("Audio file must be WAV format mono PCM.")
        return

    # Recognizer 초기화 (모델 재사용)
    # 초기 문구 리스트 설정
    rec = KaldiRecognizer(model,
        wf.getframerate(),
        '["헬로 칩스","등 켜줘", "등 꺼 줘", "에어컨 켜줘", "에어컨 꺼 줘","음악 재생해줘","음악 틀어 줘", "음악 멈춰줘", "음악 꺼줘","다음 곡 으로 넘겨줘", "다음 곡 재생","이전 곡 으로 넘겨줘", "이전 곡 재생","음량 키q\
             줘", "볼륨 높여 줘","음량 낮춰 줘", "볼륨 줄여 줘","[unk]"]')
    start = time.time()

    rec.SetGrammar( '["헬로 칩스","등 켜줘", "등 꺼 줘", "에어컨 켜줘", "에어컨 꺼 줘","음악 재생","음악 틀어 줘", "음악 정지", "음악 꺼 줘","다음 곡 으로 넘겨줘", "다음 곡 재생","이전 곡 으로 넘겨줘", "이전 곡 재생","음량 키워 줘", "볼륨 높여 줘","음량 낮춰 줘", "볼륨 줄여 줘","[unk]"]')
    # 음성 데이터 처리
    while True:
        data = wf.readframes(4000)
        if len(data) == 0:
            break
        if rec.AcceptWaveform(data):
            print("완전한 결과:", rec.Result())
            # 런타임 문구 리스트 업데이트

        else:
            print("부분 결과:", rec.PartialResult())
    elapsed_time = round(time.time() - start, 2)
    result_json = json.loads(rec.FinalResult())
    recognized_text = result_json.get("text", "").strip()
    print(f"[소요 시간: {elapsed_time}]  STT 결과:", recognized_text)

    matches = process.extract(recognized_text, commands.keys())
    for match in matches:
        print(f"- '{match[0]}' -> 점수: {match[1]}")

    best_match = process.extractOne(recognized_text, commands.keys())

    if best_match and best_match[1] > 70:  # 70% 이상의 유사도로 매칭된 경우
        command_text = best_match[0]
        command_number = commands[command_text]
        print(f"[서버] 인식된 명령어: {command_text}, 번호: {command_number}")
    else:
        print("[서버] 명령어를 인식하지 못했습니다.")

def compare_audio_saving(sample_rate=60000):
    # Compare durations
    with wave.open("recorded_audio_wave.wav", "rb") as wf:
        wave_duration = wf.getnframes() / wf.getframerate()
        print(f"DEBUG: wave duration = {wave_duration}s")

    audio_segment = AudioSegment.from_file("recorded_audio_pydub.wav")
    pydub_duration = len(audio_segment) / 1000.0
    print(f"DEBUG: pydub duration = {pydub_duration}s")


def main():
    #global stop_recording_event
    #stop_recording_event = threading.Event()
    
    #client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    #client_socket.connect((SERVER_IP, SERVER_PORT))
    #print(f"서버에 연결되었습니다. (IP: {SERVER_IP}, Port: {SERVER_PORT})")

    # 데이터 수신 스레드 시작
    #recv_thread = threading.Thread(target=receive_data, args=(client_socket,))
    #recv_thread.daemon = True
    #recv_thread.start()

    while True:
        user_input = input("녹음을 시작하려면 's'를 입력하고 엔터를 누르세요. 종료하려면 'q'를 입력하고 엔터를 누르세요: ").strip().lower()
        if user_input == 's':
            #stop_recording_event.clear()
            filename = "recorded_audio.wav"
            #filename = record_audio(filename)
            #convert_mp3_to_wav(filename, "recorded_audio.wav")
            
            # 별도의 스레드에서 녹음 시작
            #recording_thread = threading.Thread(target=record_audio, args=(filename,))
            #recording_thread.start()
            
            # 'q'를 입력받으면 녹음 중지
            # while True:
            #     stop_input = input("녹음을 중지하려면 'q'를 입력하세요: ").strip().lower()
            #     if stop_input == 'q':
            #         stop_recording_event.set()
            #         recording_thread.join()  # 녹음 스레드가 종료될 때까지 대기
            #         break
            
            # 노이즈 제거 및 STT 수행
            #sysf.off_rgb_led()
            vad_for_hellochips()
            noise_reduced_file = reduce_noise("post_vad.wav")
            #filename = input()
            transcribe_audio(noise_reduced_file)

            # if command_number is not None:
            #     print(f"[STT 완료] 명령어 번호: {command_number}")
            #     send_command_to_server(client_socket, command_number)
        
        elif user_input == 'q':
            print("종료합니다.")
            #client_socket.close()
            break
        else:
            print("잘못된 입력입니다. 's' 또는 'q'를 입력하세요.")
if __name__ == "__main__":
    main()