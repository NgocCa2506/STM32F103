#include <stdint.h>
#include <stdio.h>

// Định nghĩa các địa chỉ thanh ghi
#define RCC_APB2ENR   (*(volatile unsigned int *)0x40021018)  // Thanh ghi bật xung nhịp cho các module APB2
#define RCC_APB1ENR   (*(volatile unsigned int *)0x4002101C) // Thanh ghi kích hoạt clock APB1
#define GPIOA_CRL     (*(volatile unsigned int *)0x40010800)  // Thanh ghi cấu hình chân GPIOA 0 đến 7
#define GPIOB_CRL     (*(volatile unsigned int *)0x40010C00) // Thanh ghi cấu hình PB0 đến PB7
#define GPIOA_CRH     (*(volatile unsigned int *)0x40010804)  // Thanh ghi cấu hình chân GPIOA 8 đến 15
#define GPIOA_BSRR    (*(volatile unsigned int *)0x40010810)  // Thanh ghi set/reset bit chân GPIOA

#define I2C1_CR1      (*(volatile unsigned int *)0x40005400) // Thanh ghi điều khiển 1 I2C
#define I2C1_CR2      (*(volatile unsigned int *)0x40005404) // Thanh ghi điều khiển 1 I2C
#define I2C1_DR       (*(volatile unsigned int *)0x40005410) // Thanh ghi dữ liệu I2C
#define I2C1_SR1      (*(volatile unsigned int *)0x40005414) // Thanh ghi trạng thái 1 I2C
#define I2C1_SR2      (*(volatile unsigned int *)0x40005418) // Thanh ghi trạng thái 2 I2C
#define I2C1_CCR      (*(volatile unsigned int *)0x4000541C) // Điều khiển clock I2C

#define USART1_SR     (*(volatile unsigned int *)0x40013800)  // Thanh ghi trạng thái USART1
#define USART1_DR     (*(volatile unsigned int *)0x40013804)  // Thanh ghi dữ liệu USART1
#define USART1_BRR    (*(volatile unsigned int *)0x40013808)  // Thanh ghi baud rate USART1
#define USART1_CR1    (*(volatile unsigned int *)0x4001380C)  // Thanh ghi điều khiển 1 USART1

volatile float temperature = 0.0;
volatile int32_t t_fine;

volatile uint16_t dig_T1;
volatile int16_t dig_T2, dig_T3;

// Hàm delay
void Delay(void) {
    for (uint32_t i = 0; i < 500000; i++) {
    }
}

//Khởi tạo I2C
void I2C_Init(void) {
    RCC_APB2ENR |= (1 << 3);    // Kích hoạt clock cho GPIOB
    RCC_APB1ENR |= (1 << 21);   // Kích hoạt clock cho I2C1

    GPIOB_CRL &= ~((0xF << 24) | (0xF << 28));  // Xóa cấu hình cũ của PB6 và PB7
    GPIOB_CRL |=  (0xF << 24) | (0xF << 28);    // Cấu hình PB6 và PB7 cho I2C

    I2C1_CR2 |= 36;             // Cấu hình tần số PCLK1 cho I2C1 (36MHz là tần số của bus APB1)

    I2C1_CCR |= (0 << 15);      // SM Mode I2C

    I2C1_CCR = 180;             // Cấu hình tần số chia xung clock 100kHz

    I2C1_CR1 |= (1 << 0);       // Kích hoạt I2C
}

// Hàm Start I2C
void I2C_Start(void) {
    I2C1_CR1 |= (1 << 8);       	// Thiết lập bit START trong thanh ghi CR1
    while (!(I2C1_SR1 & (1 << 0))); // Chờ cho đến khi bit SB trong SR1 được set
}

// Hàm Stop I2C
void I2C_Stop(void) {
    I2C1_CR1 |= (1 << 9);        // Thiết lập bit STOP trong thanh ghi CR1
    while (I2C1_SR2 & (1 << 1)); // Chờ cho đến khi bus không còn bận
}


void I2C_WriteAddress(uint8_t address, uint8_t read) {
    I2C1_DR = (address << 1) | read;	// Gửi địa chỉ của thiết bị slave cùng với bit đọc/ghi
    while (!(I2C1_SR1 & (1 << 1)));  	// Chờ cho đến khi Địa chỉ nhận được trùng khớp.
    (void)I2C1_SR2; 					// Đọc thanh ghi SR2 để xóa bit ADDR
}

void I2C_WriteByte(uint8_t data) {
    I2C1_DR = data;						// Ghi byte dữ liệu vào thanh ghi I2C1_DR
    while (!(I2C1_SR1 & (1 << 7)));  	// Chờ cho đến khi bit TXE (Transmit Data Empty)
}

uint8_t I2C_ReadByte(uint8_t ack) {
    if (ack)
        I2C1_CR1 |= (1 << 10);  		// Nếu ack là 1, thiết lập bit ACK trong CR1
    else
        I2C1_CR1 &= ~(1 << 10); 		// Nếu ack là 0, xóa bit ACK trong CR1
    while (!(I2C1_SR1 & (1 << 6)));  	// Chờ cho đến khi dữ liệu được nhận hoàn tất
    return I2C1_DR;						// Trả về byte dữ liệu đã nhận
}

// Hàm đọc giá trị hiệu chuẩn
void BMP280_ReadCalibration(void) {
    I2C_Start();                             // Bắt đầu giao tiếp I2C
    I2C_WriteAddress(0x76, 0);               // Gửi địa chỉ của BMP280 và yêu cầu ghi
    I2C_WriteByte(0x88);                     // Gửi địa chỉ thanh ghi hiệu chỉnh
    I2C_Start();                             // Gửi Start lần thứ hai để chuyển sang chế độ đọc
    I2C_WriteAddress(0x76, 1);               // Gửi địa chỉ của BMP280 và yêu cầu đọc

    // Đọc các giá trị hiệu chỉnh (2 byte cho mỗi giá trị)
    dig_T1 = (I2C_ReadByte(1) | (I2C_ReadByte(1) << 8));
    dig_T2 = (I2C_ReadByte(1) | (I2C_ReadByte(1) << 8));
    dig_T3 = (I2C_ReadByte(1) | (I2C_ReadByte(0) << 8));  // Byte cuối cùng gửi NACK

    I2C_Stop();                              // Dừng giao tiếp I2C
}

// Hàm tính toán nhiệt độ thực
int32_t bmp280_compensate_T_int32(int32_t adc_T) {
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
            ((int32_t)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

// Hàm khởi tạo BMP280
void BMP280_Init(void) {
    BMP280_ReadCalibration();  // Đọc các giá trị hiệu chỉnh từ BMP280
    I2C_Start();               // Bắt đầu giao tiếp I2C
    I2C_WriteAddress(0x76, 0); // Gửi địa chỉ của BMP280 và yêu cầu ghi (write)
    I2C_WriteByte(0xF4); 	   // Gửi địa chỉ thanh ghi điều khiển đo (Control Measurement Register)
    I2C_WriteByte(0x27);       // Gửi giá trị cấu hình để bắt đầu đo nhiệt độ và áp suất (0b00100111)
    I2C_Stop();                // Dừng giao tiếp I2C
}

// Hàm đọc nhiệt độ thực
void BMP280_ReadTemperature(void) {
    uint8_t msb, lsb, xlsb;
    int32_t adc_T;

    I2C_Start();                    // Bắt đầu giao tiếp I2C
    I2C_WriteAddress(0x76, 0);      // Gửi địa chỉ của BMP280 và yêu cầu ghi
    I2C_WriteByte(0xFA);            // Gửi địa chỉ thanh ghi chứa byte nhiệt độ MSB (Most Significant Byte)

    I2C_Start();                    // Bắt đầu giao tiếp I2C mới để đọc
    I2C_WriteAddress(0x76, 1);      // Gửi địa chỉ của BMP280 và yêu cầu đọc
    msb = I2C_ReadByte(1);          // Đọc byte MSB của giá trị nhiệt độ
    lsb = I2C_ReadByte(1);          // Đọc byte LSB của giá trị nhiệt độ
    xlsb = I2C_ReadByte(0);         // Đọc byte XLSB của giá trị nhiệt độ
    I2C_Stop();                     // Dừng giao tiếp I2C

    // Ghép ba byte dữ liệu MSB, LSB và XLSB thành một giá trị 20-bit
    adc_T = ((int32_t)msb << 12) | ((int32_t)lsb << 4) | ((int32_t)xlsb >> 4);

    // Bù nhiệt độ và tính toán giá trị nhiệt độ thực tế
    temperature = bmp280_compensate_T_int32(adc_T) / 100.0;
}

// Khởi tạo USART1
void USART1_Init(void) {
    RCC_APB2ENR |= (1 << 2);         // Bật xung GPIOA
    RCC_APB2ENR |= (1 << 14);        // Bật xung USART1

    GPIOA_CRH &= ~((0xF << 4) | (0xF << 8)); // Clear cấu hình PA9 và PA10
    GPIOA_CRH |= ((0xB << 4) | (0x4 << 8));  // PA9: Alternate function push-pull, PA10: Input floating

    USART1_BRR = 0x0045;            // Cấu hình baud rate = 115200 với xung 8MHz
    USART1_CR1 &= ~((1 << 12) | (1 << 10)); // 8-bit data, không parity
    USART1_CR1 |= (1 << 3) | (1 << 2) | (1 << 13);  // Kích hoạt truyền, nhận, và USART
}

// Hàm truyền dữ liệu qua USART1
void USART1_Transmit(uint8_t data) {
    while (!(USART1_SR & (1 << 7))); // Chờ đến khi bộ đệm truyền rỗng
    USART1_DR = data;                // Gửi dữ liệu
}

// Hàm truyền chuỗi qua USART1
void USART1_Transmit_String(const char *str) {
    for (; *str != '\0'; str++) { // Duyệt từng ký tự đến khi gặp ký tự kết thúc chuỗi '\0'
        USART1_Transmit((uint8_t)*str);
    }
}

// Hàm truyền số float qua USART1
void USART1_Transmit_Float(float number) {
    char buffer[32];
    sprintf(buffer, "%.3f", number);  // Chuyển số float thành chuỗi với 3 chữ số thập phân
    USART1_Transmit_String(buffer);
}

// Hàm main
int main(void) {
	I2C_Init();
	BMP280_Init();
    USART1_Init();      // Khởi tạo USART1

    while (1) {
    	BMP280_ReadTemperature();

        USART1_Transmit_String("Temperature: ");               // Gửi chuỗi
        USART1_Transmit_Float(temperature);                         // Gửi nhiệt độ
        USART1_Transmit_String(" C\r\n");                      // Gửi ký tự xuống dòng
        Delay();                                               // Thời gian delay giữa các lần đọc
    }
}
