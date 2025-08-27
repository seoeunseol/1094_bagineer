from flask import Flask, render_template, jsonify
import threading
import socket
import logging
from flask import Flask
from flask import request

app = Flask(__name__)

log = logging.getLogger('werkzeug')
log.setLevel(logging.WARNING)

battery_packs = [
    {'id': 1, 'temp': 0, 'status': '대기'}, 
    {'id': 2, 'temp': 0, 'status': '대기'},
    {"id": 3, "pressure1": 0, "pressure2" : 0, "status": "대기"},
    {"id": 4, "pressure1": 0, "pressure2" : 0, "status": "대기"}
]

relay_state = {1: "OFF", 2: "OFF"}

press_error = [0,0]
temp_error = [0,0]

def handle_client(conn, addr):
    global battery_packs, temperature_data
    print(f"클라이언트 접속 처리 시작: {addr}")

    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print(f"클라이언트 연결 종료: {addr}")
                break

            data_str = data.decode('utf-8').strip()
            print(f"수신 데이터 ({addr}): {repr(data_str)}")

            parts = data_str.split('|')
            if len(parts) == 4:
                try:
                    p1 = int(parts[0].strip())
                    p2 = int(parts[1].strip())
                    p3 = int(parts[2].strip())
                    p4 = int(parts[3].strip())

                    battery_packs[2]['pressure1'] = p1
                    battery_packs[2]['pressure2'] = p2
                    press_error[0] = 1 if (abs(p1) > 50 or abs(p2) > 50) else 0

                    battery_packs[3]['pressure1'] = p3
                    battery_packs[3]['pressure2'] = p4
                    press_error[1] = 1 if (abs(p3) > 50 or abs(p4) > 50) else 0

                except Exception as e:
                    print(f"[압력] 데이터 파싱 오류: {e} - parts: {parts}")

                battery_packs[2]['status'] = '정상' if press_error[0] == 0 else '충격 감지'
                battery_packs[3]['status'] = '정상' if press_error[1] == 0 else '충격 감지'


            elif len(parts) == 2:
                # 온도 평균 처리
                for i, part in enumerate(parts):
                    if i >= len(battery_packs):
                        break
                    try:
                        value = int(part.strip())
                        battery_packs[i]['temp'] = value
                        if (value > 60) :
                            temp_error[i] = 1
                        else : 
                            temp_error[i] = 0                     
                    except Exception as e:
                        print(f"[온도] 데이터 파싱 오류: {e} - 부분 데이터: {part}")
                if temp_error[0] == 0:
                    battery_packs[0]['status'] = '정상'
                elif temp_error[0] == 1:
                    battery_packs[0]['status'] = '배터리 과부화'

                if temp_error[1] == 0:
                    battery_packs[1]['status'] = '정상'
                elif temp_error[1] == 1:
                    battery_packs[1]['status'] = '배터리 과부화'


            else:
                print("예상치 못한 데이터 형식")

    except Exception as e:
        print(f"클라이언트 통신 중 오류 발생: {e}")
    finally:
        conn.close()
        print(f"클라이언트 연결 종료 완료: {addr}")


def tcp_server():
    HOST = '0.0.0.0'
    PORT = 9000

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen(5)
    print(f"TCP 서버 시작: {HOST}:{PORT}")

    while True:
        print("클라이언트 연결 대기 중...")
        try:
            conn, addr = server_socket.accept()
            print(f"연결됨: {addr}")

            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()

        except Exception as e:
            print(f"연결 처리 중 오류 발생: {e}")

    server_socket.close()


def send_to_c(relay_num, state):
    try:
        c_host = '172.20.10.2'
        c_port = 9100  # C 코드에서 대기할 포트
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((c_host, c_port))
            msg = f"{relay_num}|{state}\n"
            s.sendall(msg.encode())
    except Exception as e:
        print(f"C 코드 전송 실패: {e}")

@app.route('/')
def home():
    return render_template('index.html', battery_packs=battery_packs)

@app.route('/api/battery_status')
def battery_status_api():
    return jsonify(battery_packs)

@app.route("/api/relay", methods=["POST"])
def relay_control():
    data = request.get_json()
    relay_num = data.get("relayNum")
    state = data.get("state")
    print(f"릴레이 {relay_num} 상태 변경: {state}")
    send_to_c(relay_num, state)  # C 코드로 전달
    return jsonify({"result": "success"})

if __name__ == '__main__':
    threading.Thread(target=tcp_server, daemon=True).start()
    app.run(debug=False, host='0.0.0.0', use_reloader=False)

