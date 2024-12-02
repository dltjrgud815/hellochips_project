import alsaaudio
import wave
import numpy as np
import noisereduce as nr
from faster_whisper import WhisperModel
import threading
import time
from rapidfuzz import fuzz, process
import socket

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

def send_command_to_server(client_socket, command_number):
    """
    클라이언트로 명령어 번호를 전송
    """
    try:
        client_socket.sendall(str(command_number).encode("utf-8"))
        print(f"[서버] 명령어 번호 {command_number}를 클라이언트로 전송했습니다.")
    except (ConnectionResetError, BrokenPipeError):
        print("[서버] 클라이언트 연결이 종료되었습니다.")

def send_data(client_socket):
    """
    클라이언트로 명령을 전송하는 스레드 함수
    """
    while True:
        try:
            # 사용자 입력
            command = input("[서버] 클라이언트에 보낼 명령 입력 (1: 일시 정지/재생, 2: 다음 곡, 3: 이전곡): ").strip()
            if command not in ["1", "2", "3"]:
                print("[서버] 잘못된 입력입니다. 1, 2, 3 중 하나를 입력하세요.")
                continue

            # 클라이언트에 명령 전송
            client_socket.sendall(command.encode("utf-8"))
        except (ConnectionResetError, BrokenPipeError):
            print("[서버] 클라이언트 연결이 종료되었습니다.")
            break

class CustomFasterWhisper:
    def __init__(self, model_name="tiny"):
        '''Whisper 모델 초기화'''
        self.model = WhisperModel(model_name, device="cpu", compute_type="int8", cpu_threads=8)
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

# STT 모델 로드
model = CustomFasterWhisper("tiny")

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

def stt(filename):
    """STT 모델로 변환된 텍스트 출력"""
    with wave.open(filename, "rb") as f:
        audio_data = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)
    
    print("STT 변환 중...")
    segments, _ = model.transcribe(audio_data, language="ko")
    result_text = ''.join([segment.text for segment in segments])
    print("STT 결과:", result_text)
    return result_text

def main():
    global stop_recording_event
    stop_recording_event = threading.Event()

    # 서버 소켓 생성
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # 서버 소켓 바인딩
    server_socket.bind(("0.0.0.0", SERVER_PORT))
    server_socket.listen(1)
    print(f"[서버] {SERVER_PORT} 포트에서 대기 중입니다...")

    client_socket, client_address = server_socket.accept()
    print(f"[서버] 클라이언트 연결됨: {client_address}")

    while True:
        user_input = input("녹음을 시작하려면 's'를 입력하고 엔터를 누르세요. 종료하려면 'q'를 입력하고 엔터를 누르세요: ").strip().lower()
        if user_input == 's':
            stop_recording_event.clear()
            filename = "recorded_audio.wav"
            
            # 별도의 스레드에서 녹음 시작
            recording_thread = threading.Thread(target=record_audio, args=(filename,))
            recording_thread.start()
            
            # 'q'를 입력받으면 녹음 중지
            while True:
                stop_input = input("녹음을 중지하려면 'q'를 입력하세요: ").strip().lower()
                if stop_input == 'q':
                    stop_recording_event.set()
                    recording_thread.join()  # 녹음 스레드가 종료될 때까지 대기
                    break
            
            # 노이즈 제거 및 STT 수행
            noise_reduced_file = reduce_noise(filename)
            command_number = model.run(noise_reduced_file)
            
            if command_number is not None:
                send_command_to_server(client_socket, command_number)
        
        elif user_input == 'q':
            print("종료합니다.")
            client_socket.close()
            server_socket.close()
            break
        else:
            print("잘못된 입력입니다. 's' 또는 'q'를 입력하세요.")

if __name__ == "__main__":
    main()
