#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h> 

#define SPI_CHANNEL 0
#define SPI_SPEED 1000000 // 1 MHz

int error = 0;
int delay_count = 0;

#define SERVER_IP "127.0.0.1" 
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
    int state = 0;

    float mean[8] = {0};
    int problem[8] = {0};
    int value[8]= {0};
    char send_buf[128];

    while (1) 
    {
        value[0] = readSensor(spiHandle, 0); 
        value[1] = readSensor(spiHandle, 1); 
        value[2] = readSensor(spiHandle, 2); 
        value[3] = readSensor(spiHandle, 3); 

        for (int i = 0; i < 8 ; i++)
            mean[i] = (float)((mean[i] + value[i]) / 2);

        for (int i = 0; i < 8 ; i++)
        {
            if (abs(mean[i] - value[i]) > 20)
                printf("%d번째 베터리 충격 감지\n", i+1);
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
