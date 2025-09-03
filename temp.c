#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <wiringPi.h>
#include <arpa/inet.h>

#define RELAY0 0  // GPIO17 (릴레이0)
#define RELAY1 1  // GPIO18 (릴레이1)

#define SERVER_IP "192.168.0.100"  // 서버 IP
#define SERVER_PORT 5000           // 서버 포트

// SPI로 ADC에서 값 읽기
uint16_t read_adc(int fd, uint8_t channel) {
    uint8_t tx[] = { 1, (8 + channel) << 4, 0 };
    uint8_t rx[3] = { 0 };

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .delay_usecs = 0,
        .speed_hz = 1000000,
        .bits_per_word = 8,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI 전송 실패");
        return -1;
    }

    return ((rx[1] & 3) << 8) | rx[2];
}

// ADC 값 → 온도 변환
float convert_to_temp(uint16_t adc_value) {
    float voltage = (adc_value * 3.3) / 1023.0;
    return voltage * 100;  // LM35: 10mV = 1°C
}

// 채널별 온도 읽기
float read_and_convert_temp(int fd, int channel) {
    uint16_t adc_value = read_adc(fd, channel);
    if (adc_value == (uint16_t)-1) return -1.0;
    return convert_to_temp(adc_value);
}

// 릴레이 제어
void relayOn(int relay) {
    digitalWrite(relay, HIGH);
}

void relayOff(int relay) {
    digitalWrite(relay, LOW);
}

int main() {
    int spi_fd_0, sock;
    struct sockaddr_in serv_addr;

    // SPI 장치 열기
    spi_fd_0 = open("/dev/spidev0.0", O_RDWR);
    if (spi_fd_0 < 0) {
        perror("SPI 장치 열기 실패");
        return -1;
    }

    // SPI 모드, 비트수, 속도 설정
    uint8_t mode = 0;
    uint8_t bits = 8;
    uint32_t speed = 1000000;

    ioctl(spi_fd_0, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd_0, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd_0, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    // wiringPi 초기화
    wiringPiSetup();
    pinMode(RELAY0, OUTPUT);
    pinMode(RELAY1, OUTPUT);

    // 소켓 생성
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("소켓 생성 실패");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("IP 변환 실패");
        return -1;
    }

    // 서버 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("서버 연결 실패");
        return -1;
    }

    while (1) {
        float temps[6];

        for (int i = 0; i < 6; i++) {
            temps[i] = read_and_convert_temp(spi_fd_0, i);
            if (temps[i] < 0) {
                printf("CH%d: 센서 오류\n", i);
            }
            else {
                printf("CH%d: %.2f°C\n", i, temps[i]);
            }
        }

        // 🔥 그룹별 릴레이 제어
        int relay0_trigger = 0;
        int relay1_trigger = 0;

        // 그룹 0 (CH0,1,2)
        for (int i = 0; i < 3; i++) {
            if (temps[i] >= 60.0) {   // 변경됨
                relay0_trigger = 1;
                break;
            }
        }

        // 그룹 1 (CH3,4,5)
        for (int i = 3; i < 6; i++) {
            if (temps[i] >= 60.0) {   // 변경됨
                relay1_trigger = 1;
                break;
            }
        }

        if (relay0_trigger) {
            relayOn(RELAY0);
            printf("⚡ CH0~2 중 하나 60도 이상 → 릴레이 0 ON\n");
        }
        else {
            relayOff(RELAY0);
            printf("✅ CH0~2 정상 → 릴레이 0 OFF\n");
        }

        if (relay1_trigger) {
            relayOn(RELAY1);
            printf("⚡ CH3~5 중 하나 60도 이상 → 릴레이 1 ON\n");
        }
        else {
            relayOff(RELAY1);
            printf("✅ CH3~5 정상 → 릴레이 1 OFF\n");
        }

        // 서버로 온도 데이터 전송
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
            "CH0:%.2f, CH1:%.2f, CH2:%.2f, CH3:%.2f, CH4:%.2f, CH5:%.2f\n",
            temps[0], temps[1], temps[2], temps[3], temps[4], temps[5]);

        send(sock, buffer, strlen(buffer), 0);

        sleep(1);
    }

    close(spi_fd_0);
    close(sock);

    return 0;
}
