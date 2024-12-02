import alsaaudio
import wave
import numpy as np
import noisereduce as nr
from faster_whisper import WhisperModel
import threading
import time
from rapidfuzz import fuzz, process
import socket
import webrtcvad
from scipy.signal import butter, lfilter
from concurrent.futures import ThreadPoolExecutor
import sysfs_control as sysf

SERVER_PORT = 5000
BUFFER_SIZE = 1024

commands = ["Hello Chips","헬로 칩스","등 켜줘", "등 꺼줘", "에어컨 켜줘", "에어컨 꺼줘", "음악 재생해줘", "음악 멈춰줘", "다음곡으로 넘겨줘", "이전곡으로 넘겨줘"]

# 명령어와 번호 매핑
commands = {
    "음악 재생해줘": 0,
    "음악 멈춰줘": 1,
    "다음 곡 재생해줘": 2,
    "이전 곡 재생해줘": 3
}

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

class CustomFasterWhisper:
    def __init__(self, model_name="base"):
        '''Whisper 모델 초기화'''
        self.model = WhisperModel(model_name, device="cpu", compute_type="int8", cpu_threads=4)
        print("Whisper 모델 초기화 완료")

    def run(self, audio_data):
        '''녹음된 오디오 데이터로 STT 수행'''
        start = time.time()
        segments, _ = self.model.transcribe(audio_data, beam_size=5, word_timestamps=True, language="ko")

        # STT 결과 출력
        transcript = " ".join([word.word for segment in segments for word in segment.words if segment.no_speech_prob < 0.6])
        elapsed_time = round(time.time() - start, 2)
        print(f"Model Name : tiny STT 결과: {transcript} (처리 시간: {elapsed_time}s)")
        # 명령어 매칭
        best_match = process.extractOne(transcript, commands.keys())
        if best_match and best_match[1] > 70:  # 70% 이상의 유사도로 매칭된 경우
            command_text = best_match[0]
            command_number = commands[command_text]
            print(f"[서버] 인식된 명령어: {command_text}, 번호: {command_number}")
            return command_number
        else:
            print("[서버] 명령어를 인식하지 못했습니다.")
            return None


def record_audio_with_vad(filename, sample_rate=16000, vad_sensitivity=3, frame_duration=20):
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

    sysf.control_rgb_led_async()

    silence_start_time = time.time()
    max_silence_duration = 1.0  # 1초 동안 음성 없음 감지 시 종료
    frame_size = int(sample_rate * (frame_duration / 1000)) * 2  # 20ms 프레임 크기 계산

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

        # 음성 구간만 필터링
    filtered_audio = filter_voice_segments(audio_data, sample_rate, vad)

    # 필터링된 데이터를 파일로 저장
    if filtered_audio.size > 0:
        with wave.open(filename, "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(sample_rate)
            f.writeframes(filtered_audio.astype(np.int16).tobytes())
        print("녹음 완료:", filename)
        return filename
    else:
        print("녹음된 음성 구간이 없어 파일이 생성되지 않았습니다.")
        return None

    with wave.open(filename, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(audio_data.astype(np.int16).tobytes())
    print("녹음 완료:", filename)
    return filename

# 음성 구간만 추출
def filter_voice_segments(audio_data, sample_rate, vad, frame_duration=20):
    """
    VAD를 사용하여 음성 구간만 필터링합니다.
    """
    if sample_rate not in [8000, 16000, 32000, 48000]:
        raise ValueError(f"[VAD 오류] 지원되지 않는 샘플 레이트입니다: {sample_rate}")

    frame_size = int(sample_rate * frame_duration / 1000)  # 프레임 크기 계산
    voice_segments = []

    # 프레임 단위로 데이터 분할 및 VAD 처리
    for i in range(0, len(audio_data), frame_size):
        frame = audio_data[i:i + frame_size]
        
        # 프레임이 정확한 크기인지 확인
        if len(frame) != frame_size:
            print(f"[VAD 오류] 프레임 크기가 잘못되었습니다. 예상 크기: {frame_size}, 실제 크기: {len(frame)}")
            continue

        # 데이터가 정규화된 범위인지 확인
        frame = np.clip(frame, -32768, 32767).astype(np.int16)

        try:
            if vad.is_speech(frame.tobytes(), sample_rate):
                print("[VAD] 음성 감지됨!")
                voice_segments.append(frame)
            else:
                print("[VAD] 음성 감지되지 않음.")
        except Exception as e:
            print(f"[VAD 오류] 프레임 처리 중 오류 발생: {e}")
            continue

    if len(voice_segments) == 0:
        print("[필터링] 음성 구간이 없습니다.")
        return np.array([], dtype=np.int16)

    return np.concatenate(voice_segments)

# STT 모델 로드
model = CustomFasterWhisper("base")

def record_audio(filename, sample_rate=16000):
    """사용자가 'q'를 입력할 때까지 마이크에서 오디오를 녹음하여 파일로 저장"""
    inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE, alsaaudio.PCM_NORMAL, device="plughw:1,0")
    inp.setchannels(1)
    inp.setrate(sample_rate)
    inp.setformat(alsaaudio.PCM_FORMAT_S16_LE)
    inp.setperiodsize(1024)

    audio_data = []
    print("녹음 시작... 'q'를 입력하여 녹음을 중지하세요.")

    # 'q'가 입력될 때까지 녹음
    while not stop_recording_event.is_set():
        _, data = inp.read()
        audio_data.append(np.frombuffer(data, dtype=np.int16))

    # 오디오 데이터를 하나의 numpy 배열로 결합
    audio_data = np.concatenate(audio_data)
    with wave.open(filename, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(audio_data.tobytes())
    print("녹음 완료:", filename)
    return filename

def reduce_noise(filename):
    """녹음된 파일에서 노이즈 제거 후 새로운 파일로 저장"""
    with wave.open(filename, "rb") as f:
        sample_rate = f.getframerate()
        channels = f.getnchannels()
        audio_data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    
    # 노이즈 제거 수행
    reduced_noise = nr.reduce_noise(y=audio_data, sr=sample_rate)
    noise_reduced_filename = "noise_reduced_" + filename
    with wave.open(noise_reduced_filename, "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(sample_rate)
        f.writeframes(reduced_noise.astype(np.int16).tobytes())
    print("노이즈 제거 완료:", noise_reduced_filename)
    return noise_reduced_filename

def main():
    global stop_recording_event
    stop_recording_event = threading.Event()

    while True:
        user_input = input("녹음을 시작하려면 's'를 입력하고 엔터를 누르세요. 종료하려면 'q'를 입력하고 엔터를 누르세요: ").strip().lower()
        if user_input == 's':
            #stop_recording_event.clear()
            filename = "recorded_audio.wav"
            filename = record_audio_with_vad(filename)

            sysf.off_rgb_led()

            noise_reduced_file = reduce_noise(filename)

            with wave.open(noise_reduced_file, "rb") as f:
                sample_rate = f.getframerate()
                audio_data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
            
            print("[STT] 음성 구간 필터링 중...")
            vad = webrtcvad.Vad()
            vad.set_mode(3)  # VAD 민감도 설정
            filtered_audio = filter_voice_segments(audio_data, sample_rate, vad)

            if len(filtered_audio) == 0:
                print("[STT] 음성 구간이 감지되지 않았습니다. STT를 건너뜁니다.")
                continue
            

            filtered_filename = "filtered_audio.wav"
            with wave.open(filtered_filename, "wb") as f:
                f.setnchannels(1)
                f.setsampwidth(2)
                f.setframerate(sample_rate)
                f.writeframes(filtered_audio.tobytes())
            print(f"[STT] 필터링된 오디오가 저장되었습니다: {filtered_filename}")
            
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
            
            command_number = model.run(filtered_audio)
            if command_number is not None:
                print(f"[STT 완료] 인식된 명령어 번호: {command_number}")
            else:
                print("[STT] 명령어를 인식하지 못했습니다.")

            #배치 처리로 STT 수행
            
        
        elif user_input == 'q':
            print("종료합니다.")
            #client_socket.close()
            #server_socket.close()
            break
        else:
            print("잘못된 입력입니다. 's' 또는 'q'를 입력하세요.")

if __name__ == "__main__":
    main()
