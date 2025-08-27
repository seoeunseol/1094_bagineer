#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <wiringPi.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

void relayOn(int index);
void relayOff(int index);


#define SPI_DEVICE_0 "/dev/spidev0.0"
#define SPI_SPEED 1350000

#define RELAY_COUNT 2
int relayPins[RELAY_COUNT] = {0, 1};

#define TEMP_THRESHOLD 35.0

#define SERVER_IP "172.20.10.4"  
#define SERVER_PORT 9000

void* relay_tcp_server(void* arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("소켓 생성 실패");
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9100);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind 실패");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen 실패");
        close(server_fd);
        return NULL;
    }

    printf("릴레이 제어 TCP 서버 시작 (포트 9100)\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept 실패");
            continue;
        }

        char buf[64];
        int n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';  // 문자열 종료

            int relayNum;
            char state[8];
            if (sscanf(buf, "%d|%7s", &relayNum, state) == 2) {
                if (relayNum >= 1 && relayNum <= RELAY_COUNT) {
                    if (strcmp(state, "ON") == 0) 
                    {
                        relayOn(relayNum - 1);
                        printf("릴레이 %d ON\n", relayNum);
                    } 
                    else if (strcmp(state, "OFF") == 0) 
                    {
                        relayOff(relayNum - 1);
                        printf("릴레이 %d OFF\n", relayNum);
                    } else 
                    {
                        printf("알 수 없는 명령: %s\n", state);
                    }
                } else {
                    printf("릴레이 번호 범위 초과: %d\n", relayNum);
                }
            } else {
                printf("잘못된 명령 포맷: %s\n", buf);
            }
        }
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}



int read_adc(int fd, uint8_t channel) 
{
    if (channel > 7) return -1;

    uint8_t tx[] = {1, (8 + channel) << 4, 0};
    uint8_t rx[3] = {0};

    struct spi_ioc_transfer tr =
    {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .delay_usecs = 0,
        .speed_hz = SPI_SPEED,
        .bits_per_word = 8,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) 
    {
        perror("SPI_IOC_MESSAGE 실패");
        return -1;
    }

    int value = ((rx[1] & 3) << 8) | rx[2];
    return value;
}

float convert_to_celsius(int adc_value) 
{
    float voltage = (adc_value * 3.3f) / 1023.0f;
    return voltage * 100.0f;
}

void setup_relays() 
{
    wiringPiSetup();
    for (int i = 0; i < RELAY_COUNT; i++)
    {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], HIGH); 
    }
}

void relayOff(int index) 
{
    if (index >= 0 && index < RELAY_COUNT)
        digitalWrite(relayPins[index], LOW);
}

void relayOn(int index) 
{
    if (index >= 0 && index < RELAY_COUNT)
        digitalWrite(relayPins[index], HIGH);
}

float average_temp(int fd, int ch1, int ch2, int ch3) 
{
    int val1 = read_adc(fd, ch1);
    int val2 = read_adc(fd, ch2);
    int val3 = read_adc(fd, ch3);

    float t1 = convert_to_celsius(val1);
    float t2 = convert_to_celsius(val2);
    float t3 = convert_to_celsius(val3);

    printf("CH%d: %.2f°C, CH%d: %.2f°C, CH%d: %.2f°C\n", ch1, t1, ch2, t2, ch3, t3);

    return (t1 + t2 + t3) / 3.0f;
}

int create_and_connect_socket() 
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("소켓 생성 실패");
        return -1;
    }

    struct sockaddr_in servaddr = {0};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) {
        perror("IP 주소 변환 실패");
        close(sockfd);
        return -1;
    }

    // 서버에 연결 시도
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("서버 연결 실패");
        close(sockfd);
        return -1;
    }

    return sockfd;
}


int main() {
    if (wiringPiSetup() < 0) {
        fprintf(stderr, "wiringPi 초기화 실패\n");
        return 1;
    }

    setup_relays();

    int spi_fd_0 = open(SPI_DEVICE_0, O_RDWR);
    if (spi_fd_0 < 0) {
        perror("SPI 장치 열기 실패");
        return 1;
    }

    int sockfd = create_and_connect_socket();
    if (sockfd < 0) {
        printf("서버에 연결할 수 없습니다.\n");
        close(spi_fd_0);
        return 1;
    }

    pthread_t tcp_thread;
    pthread_create(&tcp_thread, NULL, relay_tcp_server, NULL);

    while (1) {
        printf("=== 센서 그룹 온도 체크 ===\n");

        float avg1 = average_temp(spi_fd_0, 0, 1, 2);
        float avg2 = average_temp(spi_fd_0, 3, 4, 5);

        if (sockfd < 0) {
            sockfd = create_and_connect_socket();
            if (sockfd < 0) 
            {
                printf("소켓 재연결 실패, 5초 대기\n");
                sleep(5);
                continue;
            }
        }

        char msg[64];
        snprintf(msg, sizeof(msg), "%d|%d\n", (int)avg1, (int)avg2);

        ssize_t sent = send(sockfd, msg, strlen(msg), 0);
        if (sent < 0) 
        {
            perror("데이터 전송 실패, 소켓 재연결 시도");
            close(sockfd);
            sockfd = -1;
            continue;
        } 
        else 
        {
            printf("서버에 데이터 전송: %s", msg);
        }

        // 릴레이 제어
        /*float group_avgs[2] = {avg1, avg2};
        for (int i = 0; i < RELAY_COUNT; i++) {
            printf("릴레이 %d 대응 그룹 평균: %.2f°C -> ", i + 1, group_avgs[i]);
            if (group_avgs[i] >= TEMP_THRESHOLD) 
            {
                relayOff(i);
                printf("릴레이 ON\n");
            } 
            else {
                relayOn(i);
                printf("릴레이 OFF\n");
            }
        } */

        printf("--------------------------\n");
        sleep(1);
    }

    close(spi_fd_0);
    if (sockfd >= 0) close(sockfd);
    return 0;
}
