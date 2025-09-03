from flask import Flask, render_template, jsonify, request
import threading
import socket
import logging

app = Flask(__name__)

# ---------------- 로깅 설정 ----------------
log = logging.getLogger('werkzeug')
log.setLevel(logging.WARNING)

# ---------------- 배터리 데이터 초기화 ----------------
battery_packs = [
    {'id': 1, 'temp': [0, 0, 0], 'status': '대기'}, 
    {'id': 2, 'temp': [0, 0, 0], 'status': '대기'},
    {"id": 3, "pressure1": 0, "pressure2": 0, "status": "대기"},
    {"id": 4, "pressure1": 0, "pressure2": 0, "status": "대기"}
]

relay_state = {1: "OFF", 2: "OFF"}
temp_error = [0, 0]

# ---------------- 직전 압력값 및 충격 타이머 저장 ----------------
prev_press = [0, 0, 0, 0]
shock_timer = [0, 0, 0, 0]  # 단위: 루프 횟수 (0.1초 간격, 10초 유지 → 100)

# ---------------- TCP 클라이언트 처리 ----------------
def handle_client(conn, addr):
    global battery_packs, prev_press, shock_timer
    print(f"클라이언트 접속 처리 시작: {addr}")

    try:
        while True:
            data = conn.recv(1024)
            if not data:
                print(f"클라이언트 연결 종료: {addr}")
                break

            data_str = data.decode('utf-8').strip()
            parts = data_str.split('|')

            # ---------------- 온도 처리 (6개 센서, 소수점 가능) ----------------
            if len(parts) == 6:
                try:
                    temps = [round(float(x.strip()), 2) for x in parts]  # float로 변환, 소수점 2자리 반올림
                    # 배터리팩1: temps[0:3], 배터리팩2: temps[3:6]
                    battery_packs[0]['temp'] = temps[0:3]
                    battery_packs[1]['temp'] = temps[3:6]

                    # 상태 판정
                    temp_error[0] = 1 if any(t > 60.0 for t in temps[0:3]) else 0
                    temp_error[1] = 1 if any(t > 60.0 for t in temps[3:6]) else 0

                    battery_packs[0]['status'] = '정상' if temp_error[0] == 0 else '배터리 과부화'
                    battery_packs[1]['status'] = '정상' if temp_error[1] == 0 else '배터리 과부화'

                    # ---------------- 터미널 출력 ----------------
                    print(f"[온도] 배터리팩1: {temps[0]:.2f}, {temps[1]:.2f}, {temps[2]:.2f} | 상태: {battery_packs[0]['status']}")
                    print(f"[온도] 배터리팩2: {temps[3]:.2f}, {temps[4]:.2f}, {temps[5]:.2f} | 상태: {battery_packs[1]['status']}")

                except Exception as e:
                    print(f"[온도] 데이터 파싱 오류: {e} - parts: {parts}")

            # ---------------- 압력 처리 ----------------
            elif len(parts) == 4:
                try:
                    p = [int(x.strip()) for x in parts]  # p1, p2, p3, p4

                    # ---------------- 충격 감지 처리 ----------------
                    for i in range(4):
                        if abs(p[i] - prev_press[i]) > 30:
                            shock_timer[i] = 100  # 10초 유지 (0.1초 루프 기준)
                        prev_press[i] = p[i]

                    # 배터리팩3
                    if shock_timer[0] > 0 or shock_timer[1] > 0:
                        battery_packs[2]['status'] = '충격 감지'
                        battery_packs[2]['pressure1'] = p[0] if shock_timer[0] > 0 else 0
                        battery_packs[2]['pressure2'] = p[1] if shock_timer[1] > 0 else 0
                    else:
                        battery_packs[2]['status'] = '정상'
                        battery_packs[2]['pressure1'] = 0
                        battery_packs[2]['pressure2'] = 0

                    # 배터리팩4
                    if shock_timer[2] > 0 or shock_timer[3] > 0:
                        battery_packs[3]['status'] = '충격 감지'
                        battery_packs[3]['pressure1'] = p[2] if shock_timer[2] > 0 else 0
                        battery_packs[3]['pressure2'] = p[3] if shock_timer[3] > 0 else 0
                    else:
                        battery_packs[3]['status'] = '정상'
                        battery_packs[3]['pressure1'] = 0
                        battery_packs[3]['pressure2'] = 0

                    # 타이머 감소
                    for i in range(4):
                        if shock_timer[i] > 0:
                            shock_timer[i] -= 1

                    # ---------------- 터미널 출력 ----------------
                    print(f"[압력] 배터리팩3: 압력1: {battery_packs[2]['pressure1']} | 압력2: {battery_packs[2]['pressure2']} | 상태: {battery_packs[2]['status']}")
                    print(f"[압력] 배터리팩4: 압력1: {battery_packs[3]['pressure1']} | 압력2: {battery_packs[3]['pressure2']} | 상태: {battery_packs[3]['status']}")

                except Exception as e:
                    print(f"[압력] 데이터 파싱 오류: {e} - parts: {parts}")
            else:
                print("예상치 못한 데이터 형식:", parts)

    except Exception as e:
        print(f"클라이언트 통신 중 오류 발생: {e}")
    finally:
        conn.close()
        print(f"클라이언트 연결 종료 완료: {addr}")

# ---------------- TCP 서버 ----------------
def tcp_server():
    HOST = '0.0.0.0'
    PORT = 9000

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((HOST, PORT))
    server_socket.listen(5)
    print(f"TCP 서버 시작: {HOST}:{PORT}")

    while True:
        try:
            conn, addr = server_socket.accept()
            print(f"연결됨: {addr}")
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()
        except Exception as e:
            print(f"연결 처리 중 오류 발생: {e}")

    server_socket.close()

# ---------------- C 코드로 릴레이 제어 전송 ----------------
def send_to_c(relay_num, state):
    try:
        c_host = '192.168.137.23'
        c_port = 9100
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((c_host, c_port))
            msg = f"{relay_num}|{state}\n"
            s.sendall(msg.encode())
    except Exception as e:
        print(f"C 코드 전송 실패: {e}")

# ---------------- Flask 라우트 ----------------
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
    send_to_c(relay_num, state)
    return jsonify({"result": "success"})

# ---------------- 메인 ----------------
if __name__ == '__main__':
    threading.Thread(target=tcp_server, daemon=True).start()
    app.run(debug=False, host='0.0.0.0', use_reloader=False)
