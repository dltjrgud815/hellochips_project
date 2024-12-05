import cv2
import numpy as np
import os
import time
import subprocess
import socket

FLAG_FILE = '/tmp/mbox_data.log'  # flag 파일 경로
WINDOW_NAME = "Output"  # 창 이름

def send_flags_to_server(server_ip, server_port, flag1, flag2):
    try:
        # 소켓 생성
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
            # 서버에 연결
            client_socket.connect((server_ip, server_port))
            # 플래그 전송
            message = f"{flag1} {flag2}"
            client_socket.sendall(message.encode('utf-8'))
            print(f"Sent to server: {message}")
    except ConnectionRefusedError:
        print("Error: Unable to connect to the server. Is it running?")
    except Exception as e:
        print(f"Error: {e}")

def get_screen_resolution():
    # X11의 xrandr 명령을 사용하여 현재 화면 해상도를 가져옵니다.
    result = subprocess.run(['xrandr'], stdout=subprocess.PIPE)
    output = result.stdout.decode('utf-8')
    for line in output.splitlines():
        if '*' in line:
            resolution = line.split()[0]  # 화면 해상도
            width, height = map(int, resolution.split('x'))
            return width, height
    return 1920, 1080  # 기본값: 1920x1080

def read_flag():
    # flag 파일을 읽어 현재 값을 반환
    try:
        with open(FLAG_FILE, "r") as file:
            values = file.read().strip().split()  # 공백으로 값을 나눔
            # 값이 두 개일 경우 각각 변수에 할당
            if len(values) == 2:
                value1, value2 = map(int, values)  # 두 값을 정수로 변환
                return value1, value2
            else:
                print("Invalid flag format in file. Defaulting to 3 3.")
                return 3, 3
    except FileNotFoundError:
        print("Flag file not found. Defaulting to 3 3.")
        return 3, 3
    except ValueError:
        print("Invalid flag value in file. Defaulting to 3 3.")
        return 3, 3

def set_fullscreen():
    # OpenCV 창을 전체 화면으로 설정하는 함수
    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.setWindowProperty(WINDOW_NAME, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

def show_black_screen_with_text():
    # 검은 화면에 'HelloChips!' 텍스트를 출력
    screen_width, screen_height = get_screen_resolution()

    # 검은 화면
    black_frame = np.zeros((screen_height, screen_width, 3), dtype=np.uint8)

    # 텍스트 추가
    text = "HelloChips!"
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = 3
    font_color = (255, 255, 255)
    thickness = 5
    text_size = cv2.getTextSize(text, font, font_scale, thickness)[0]
    text_x = (black_frame.shape[1] - text_size[0]) // 2
    text_y = (black_frame.shape[0] + text_size[1]) // 2
    cv2.putText(black_frame, text, (text_x, text_y), font, font_scale, font_color, thickness)

    # 전체 화면에서 출력
    cv2.imshow(WINDOW_NAME, black_frame)
    cv2.waitKey(1)

def show_video(video_source='/dev/video1'):
    # 비디오를 출력 /dev/video1 장치 사용
    cap = cv2.VideoCapture(video_source)
    if not cap.isOpened():
        print("Error: Could not open video source.")
        return

    screen_width, screen_height = get_screen_resolution()

    # 비디오 캡처 설정: 전체 화면 모드
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, screen_width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, screen_height)

    while True:
        ret, frame = cap.read()
        if not ret:
            print("Error: Failed to capture frame.")
            break

        # 비디오 크기를 화면 크기에 맞게 조정
        frame_resized = cv2.resize(frame, (screen_width, screen_height))

        # 전체 화면에서 비디오 출력
        cv2.imshow(WINDOW_NAME, frame_resized)

        # flag1 값을 읽어 동작 변경 여부 확인
        flag1, _ = read_flag()
        print(f"Current Flag1 in show_video: {flag1}")  # 디버깅용 출력

        # flag1이 2가 아니면 비디오 종료
        if flag1 != 2:
            print("Exiting video mode...")
            cap.release()
            return

        # 'q' 키로 강제 종료
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()

def main():
    print("Starting program. Watching for flag changes.")
    prev_flag1 = -1
    prev_flag2 = -1
    server_ip = "10.42.0.2"
    server_port = 12345

    # OpenCV 창을 전체 화면으로 설정
    set_fullscreen()

    while True:
        # flag 읽기
        flag1, flag2 = read_flag()
        # flag1이나 flag2가 변경되었을 때만 서버로 전송
        if flag1 != prev_flag1 or flag2 != prev_flag2:
            send_flags_to_server(server_ip, server_port, flag1, flag2)

        # flag1 변경에 따른 화면 동작 처리
        if flag1 != prev_flag1:
            if flag1 == 2 or flag1==4:  # 후진 카메라 모드
                show_video('/dev/video1')
            elif flag1 == 0:  # 프로그램 종료
                cv2.destroyAllWindows()
                break
            else:  # 기본 검은 화면 모드
                show_black_screen_with_text()

        # 이전 플래그 상태 업데이트
        prev_flag1, prev_flag2 = flag1, flag2

        # OpenCV 창 이벤트 처리
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

        time.sleep(0.1)  # 짧은 대기 후 다시 확인

if __name__ == "__main__":
    main()

