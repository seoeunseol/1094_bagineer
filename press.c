#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>  // uint8_t 사용을 위한 헤더
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h> 

#define SPI_CHANNEL 0
#define SPI_SPEED 1000000 // 1 MHz

#define SERVER_IP "192.168.137.38"  // Flask 서버 IP
#define SERVER_PORT 9000

int connect_to_server()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("소켓 생성 실패");
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("서버 연결 실패");
        close(sock);
        return -1;
    }

    printf("서버에 연결됨: %s:%d\n", SERVER_IP, SERVER_PORT);
    return sock;
}

int initSPI()
{
    if (gpioInitialise() < 0)
    {
        printf("pigpio 초기화 실패\n");
        return -1;
    }

    int spiHandle = spiOpen(SPI_CHANNEL, SPI_SPEED, 0);
    if (spiHandle < 0)
    {
        printf("SPI 초기화 실패\n");
        gpioTerminate();
        return -1;
    }

    return spiHandle;
}

int readSensor(int spiHandle, int channel)
{
    char buffer[3];
    buffer[0] = 1; // Start bit
    buffer[1] = (8 + channel) << 4; // 채널 선택
    buffer[2] = 0; // 더미 데이터
    spiXfer(spiHandle, buffer, buffer, 3);
    return ((buffer[1] & 3) << 8) + buffer[2];
}

int main()
{
    int sock = connect_to_server();
    int spiHandle = initSPI();
    if (spiHandle < 0)
        return 1;

    float timeInterval = 0.1;
    int value[4] = { 0 };
    int prev[4] = { 0 };   // 직전 값 저장
    char send_buf[128];

    // ---- 초기값 읽어서 prev에 저장 ----
    for (int i = 0; i < 4; i++) {
        prev[i] = readSensor(spiHandle, i);
    }
    printf("초기값(첫 prev): %d | %d | %d | %d\n", prev[0], prev[1], prev[2], prev[3]);

    // ---- 무한 루프 ----
    while (1)
    {
        for (int i = 0; i < 4; i++) {
            value[i] = readSensor(spiHandle, i);
        }

        for (int i = 0; i < 4; i++)
        {
            if (abs(value[i] - prev[i]) > 30)
                printf("%d번째 배터리 충격 감지! (현재: %d, 이전: %d)\n", i + 1, value[i], prev[i]);

            // 직전 값 갱신
            prev[i] = value[i];
        }

        printf("충격1 : %d | 충격2: %d | 충격3 : %d | 충격4 : %d\n", value[0], value[1], value[2], value[3]);
        sprintf(send_buf, "%d | %d | %d | %d", value[0], value[1], value[2], value[3]);
        send(sock, send_buf, strlen(send_buf), 0);

        usleep((int)(timeInterval * 1000000));
    }

    // 종료 시 정리
    spiClose(spiHandle);
    gpioTerminate();
    return 0;
}
