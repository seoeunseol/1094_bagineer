import socket
import threading
import time
import random

HOST = '127.0.0.1'
SEND_PORT = 9000    # 센서 데이터 전송용
RECV_PORT = 9100    # 릴레이 데이터 수신용

def send_sensor_data():
    while True:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as send_sock:
                send_sock.connect((HOST, SEND_PORT))
                print(f"[송신] 서버에 연결됨: {HOST}:{SEND_PORT}")
                while True:
                    # 압력 데이터 (4개)
                    p1 = random.randint(-60, 60)
                    p2 = random.randint(-60, 60)
                    p3 = random.randint(-60, 60)
                    p4 = random.randint(-60, 60)
                    pressure_data = f"{p1}|{p2}|{p3}|{p4}"
                    send_sock.sendall(pressure_data.encode('utf-8'))
                    print(f"[송신] 압력 데이터 전송: {pressure_data}")
                    time.sleep(0.1)

                    # 온도 데이터 (2개)
                    t1 = random.randint(20, 80)
                    t2 = random.randint(20, 80)
                    temp_data = f"{t1}|{t2}"
                    send_sock.sendall(temp_data.encode('utf-8'))
                    print(f"[송신] 온도 데이터 전송: {temp_data}")
                    time.sleep(0.1)
        except ConnectionRefusedError:
            print("[송신] 서버 연결 실패, 1초 후 재시도...")
            time.sleep(1)
        except Exception as e:
            print(f"[송신] 오류 발생: {e}, 1초 후 재시도")
            time.sleep(1)

def receive_relay_data():
    while True:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as recv_sock:
                recv_sock.connect((HOST, RECV_PORT))
                print(f"[수신] 서버에 연결됨: {HOST}:{RECV_PORT}")
                while True:
                    data = recv_sock.recv(1024)
                    if not data:
                        print("[수신] 서버 연결 종료됨")
                        break
                    message = data.decode('utf-8').strip()
                    if message.startswith("RELAY|"):
                        parts = message.split('|')
                        if len(parts) == 3:
                            relay_num = parts[1]
                            state = parts[2]
                            print(f"[수신] 릴레이 {relay_num} 상태: {state}")
        except ConnectionRefusedError:
            print("[수신] 서버 연결 실패, 1초 후 재시도...")
            time.sleep(1)
        except Exception as e:
            print(f"[수신] 오류 발생: {e}, 1초 후 재시도")
            time.sleep(1)

if __name__ == '__main__':
    threading.Thread(target=send_sensor_data, daemon=True).start()
    threading.Thread(target=receive_relay_data, daemon=True).start()

    while True:
        time.sleep(1)
