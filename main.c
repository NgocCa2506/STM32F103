#include <stdint.h>

// Định nghĩa địa chỉ thanh ghi (register)
#define RCC_APB2ENR (*((volatile unsigned long *)0x40021018))  // Thanh ghi bật clock cho các thiết bị ngoại vi APB2
#define GPIOA_CRL   (*((volatile unsigned long *)0x40010800))  // Thanh ghi cấu hình cho các chân từ PA0 đến PA7
#define SPI1_CR1    (*((volatile unsigned long *)0x40013000))  // Thanh ghi điều khiển SPI1
#define SPI1_SR     (*((volatile unsigned long *)0x40013008))  // Thanh ghi trạng thái của SPI1
#define SPI1_DR     (*((volatile unsigned long *)0x4001300C))  // Thanh ghi dữ liệu của SPI1

int32_t temperature = 0; // Biến lưu giá trị nhiệt độ
int32_t pressure = 0;    // Biến lưu giá trị áp suất

// Khởi tạo SPI
void SPI_Init(void) {

    RCC_APB2ENR |= (1 << 2);   // Bật clock cho GPIOA
    RCC_APB2ENR |= (1 << 12);  // Bật clock cho SPI1

    GPIOA_CRL &= ~(0b1111 << 20);  // Xóa bit điều khiển cũ của PA5
    GPIOA_CRL |= (0b1011 << 20); // Thiết lập PA5 làm chân SCK với chức năng Alternate function push-pull

    GPIOA_CRL &= ~(0b1111 << 24);  // Xóa bit điều khiển cũ của PA6
    GPIOA_CRL |= (0b0100 << 24); // Thiết lập PA6 làm chân MISO với chức năng Input floating

    GPIOA_CRL &= ~(0b1111 << 28);  // Xóa bit điều khiển cũ của PA7
    GPIOA_CRL |= (0b1011 << 28); // Thiết lập PA7 làm chân MOSI với chức năng Alternate function push-pull

    SPI1_CR1 |= (1 << 2);  // MSTR: SPI ở chế độ master
    SPI1_CR1 |= (1 << 6);  // SPE: Kích hoạt SPI
    SPI1_CR1 |= (1 << 9);  // SSM: Quản lý NSS bằng phần mềm
    SPI1_CR1 |= (1 << 8);  // SSI: NSS giữ ở mức cao khi không có truyền thông
    SPI1_CR1 |= (1 << 1);  // CPOL: Đặt chân clock ở mức cao khi không truyền
    SPI1_CR1 |= (1 << 0);  // CPHA: Dữ liệu được lấy ở cạnh thứ 2 của xung clock
    SPI1_CR1 |= (0b0111 << 3);  // Đặt tốc độ baud rate cho SPI
}

// Truyền và nhận dữ liệu SPI
uint8_t SPI_Transmit_And_Receive(uint8_t data) {
    // Chờ cho đến khi bộ đệm truyền rỗng (TXE bit set)
    while (!(SPI1_SR & (1 << 1))); // Chờ bit TXE trong thanh ghi trạng thái được set (TXE: Transmit buffer empty)
    SPI1_DR = data; // Gửi dữ liệu

    // Chờ cho đến khi nhận được dữ liệu (RXNE bit set)
    while (!(SPI1_SR & (1 << 0))); // Chờ bit RXNE trong thanh ghi trạng thái được set (RXNE: Receive buffer not empty)
    return SPI1_DR; // Đọc dữ liệu nhận được từ thanh ghi dữ liệu
}

// Khởi tạo cảm biến BME280
void BME280_Init(void) {
    SPI_Transmit_And_Receive(0xF4); // Gửi lệnh điều khiển chế độ hoạt động
    SPI_Transmit_And_Receive(0x25); // Giá trị khởi tạo cho BME280, thiết lập Forced mode
}

// Đọc nhiệt độ từ cảm biến BME280
int32_t BME280_Read_Temperature(void) {
    int32_t temp_msb, temp_lsb, temp_xlsb;

    SPI_Transmit_And_Receive(0xFA); // Đọc thanh ghi nhiệt độ MSB
    temp_msb = SPI_Transmit_And_Receive(0xFF); // Gửi 0xFF và nhận lại byte từ thiết bị SPI

    SPI_Transmit_And_Receive(0xFB); // Đọc thanh ghi nhiệt độ LSB
    temp_lsb = SPI_Transmit_And_Receive(0xFF); // Gửi 0xFF và nhận lại byte từ thiết bị SPI

    SPI_Transmit_And_Receive(0xFC); // Đọc thanh ghi nhiệt độ XLSB
    temp_xlsb = SPI_Transmit_And_Receive(0xFF); // Gửi 0xFF và nhận lại byte từ thiết bị SPI

    // Kết hợp 3 byte lại thành giá trị nhiệt độ
    int32_t temp = (temp_msb << 12) | (temp_lsb << 4) | (temp_xlsb >> 4);
    return temp;
}

// Đọc áp suất từ cảm biến BME280
int32_t BME280_Read_Pressure(void) {
    int32_t press_msb, press_lsb, press_xlsb;

    SPI_Transmit_And_Receive(0xF7); // Đọc thanh ghi áp suất MSB
    press_msb = SPI_Transmit_And_Receive(0xFF);

    SPI_Transmit_And_Receive(0xF8); // Đọc thanh ghi áp suất LSB
    press_lsb = SPI_Transmit_And_Receive(0xFF);

    SPI_Transmit_And_Receive(0xF9); // Đọc thanh ghi áp suất XLSB
    press_xlsb = SPI_Transmit_And_Receive(0xFF);

    // Kết hợp 3 byte lại thành giá trị áp suất
    int32_t press = (press_msb << 12) | (press_lsb << 4) | (press_xlsb >> 4);
    return press;
}

// Tính toán và cập nhật nhiệt độ
void Calculate_Temperature(int32_t temp) {
    int32_t dig_T1 = 27504; // Hệ số hiệu chỉnh nhiệt độ
    int32_t dig_T2 = 26435;
    int32_t dig_T3 = -1000;

    int32_t var1, var2, T;
    var1 = ((((temp >> 3) - (dig_T1 << 1))) * dig_T2) >> 11;
    var2 = (((temp >> 4) - dig_T1) * ((temp >> 4) - dig_T1) * dig_T3) >> 14;
    T = var1 + var2;
    temperature = (T * 5 + 128) >> 8; // Cập nhật giá trị nhiệt độ tính toán
}

// Tính toán và cập nhật áp suất
void Calculate_Pressure(int32_t press) {
    int32_t dig_P1 = 36477; // Hệ số hiệu chỉnh áp suất
    int32_t dig_P2 = -10685;
    int32_t dig_P3 = 3024;
    int32_t dig_P4 = 2855;
    int32_t dig_P5 = 140;
    int32_t dig_P6 = -7;
    int32_t dig_P7 = 15500;
    int32_t dig_P8 = -14600;
    int32_t dig_P9 = 6000;

    int64_t var1, var2, p;
    var1 = ((int64_t)press - 128000);
    var2 = var1 * var1 * (int64_t)dig_P6;
    var2 += ((var1 * (int64_t)dig_P5) << 17);
    var2 += (((int64_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

    if (var1 == 0) {
        return;  // Tránh chia cho 0
    }

    p = 1048576 - press;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
    pressure = p / 256;  // Cập nhật giá trị áp suất tính toán
}

// Hàm delay chờ một khoảng thời gian
void delay_ms(uint32_t ms) {
    volatile uint32_t count;
    while (ms--) {
        for (count = 0; count < 1000; count++); // Vòng lặp điều chỉnh cho phù hợp với tốc độ clock của vi điều khiển
    }
}

// Hàm chính
int main(void) {
    SPI_Init();       // Khởi tạo giao thức SPI
    BME280_Init();    // Khởi tạo cảm biến BME280

    while (1) {
        int32_t temp = BME280_Read_Temperature();  // Đọc giá trị thô của nhiệt độ
        Calculate_Temperature(temp);               // Tính toán và cập nhật nhiệt độ vào biến toàn cục

        int32_t press = BME280_Read_Pressure();    // Đọc giá trị thô của áp suất
        Calculate_Pressure(press);                 // Tính toán và cập nhật áp suất vào biến toàn cục

        delay_ms(1000); // Thời gian chờ giữa các lần đọc (1 giây)
    }
}
