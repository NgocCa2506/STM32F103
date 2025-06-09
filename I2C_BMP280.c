#include <stdint.h>

#define RCC_APB2ENR   (*(volatile unsigned int *)0x40021018) // Thanh ghi kích hoạt clock APB2
#define RCC_APB1ENR   (*(volatile unsigned int *)0x4002101C) // Thanh ghi kích hoạt clock APB1
#define GPIOB_CRL     (*(volatile unsigned int *)0x40010C00) // Thanh ghi cấu hình PB0 đến PB7
#define I2C1_CR1      (*(volatile unsigned int *)0x40005400) // Thanh ghi điều khiển 1 I2C
#define I2C1_CR2      (*(volatile unsigned int *)0x40005404) // Thanh ghi điều khiển 1 I2C
#define I2C1_DR       (*(volatile unsigned int *)0x40005410) // Thanh ghi dữ liệu I2C
#define I2C1_SR1      (*(volatile unsigned int *)0x40005414) // Thanh ghi trạng thái 1 I2C
#define I2C1_SR2      (*(volatile unsigned int *)0x40005418) // Thanh ghi trạng thái 2 I2C
#define I2C1_CCR      (*(volatile unsigned int *)0x4000541C) // Điều khiển clock I2C
#define I2C1_TRISE    (*(volatile unsigned int *)0x40005420) // Thanh ghi TRISE I2C

#define BMP280_ADDRESS 0x76
#define BMP280_REG_TEMP_XLSB  0xFC // Địa chỉ thanh ghi chứa 4 bit thấp nhất (extra least significant bits)
#define BMP280_REG_TEMP_LSB   0xFB // Địa chỉ của thanh ghi chứa byte thứ hai (least significant byte)
#define BMP280_REG_TEMP_MSB   0xFA // Địa chỉ của thanh ghi chứa byte cao nhất (most significant byte)
#define BMP280_REG_CTRL_MEAS  0xF4 // Địa chỉ của thanh ghi điều khiển đo lường (Control Measurement Register)
#define BMP280_REG_CONFIG     0xF5 // Địa chỉ của thanh ghi cấu hình (Configuration Register)
#define BMP280_REG_CALIB      0x88 // Địa chỉ bắt đầu của khu vực thanh ghi hiệu chỉnh (calibration registers)

volatile float temperature = 0.0;
volatile int32_t t_fine;

volatile uint16_t dig_T1;
volatile int16_t dig_T2, dig_T3;

void I2C_Init(void) {
    RCC_APB2ENR |= (1 << 3);    // Kích hoạt clock cho GPIOB
    RCC_APB1ENR |= (1 << 21);   // Kích hoạt clock cho I2C1

    GPIOB_CRL &= ~((0xF << 24) | (0xF << 28));  // Xóa cấu hình cũ của PB6 và PB7
    GPIOB_CRL |=  (0xB << 24) | (0xB << 28);    // Cấu hình PB6 và PB7 cho I2C

    I2C1_CR2 |= 36;             // Cấu hình tần số PCLK1 cho I2C1 (36 là tần số của bus APB1)

    I2C1_CCR = 180;             // Cấu hình tần số chia xung clock 100kHz

    I2C1_TRISE = 37;            // Cấu hình TRISE

    I2C1_CR1 |= (1 << 0);   // Kích hoạt I2C
}

void I2C_Start(void) {
    I2C1_CR1 |= (1 << 8);       	// Thiết lập bit START trong thanh ghi CR1
    while (!(I2C1_SR1 & (1 << 0))); // Chờ cho đến khi bit SB trong SR1 được set
}

void I2C_Stop(void) {
    I2C1_CR1 |= (1 << 9);        // Thiết lập bit STOP trong thanh ghi CR1
    while (I2C1_SR2 & (1 << 1)); // Chờ cho đến khi bus không còn bận
}

void I2C_WriteAddress(uint8_t address, uint8_t read) {
    I2C1_DR = (address << 1) | read;	// Gửi địa chỉ của thiết bị slave cùng với bit đọc/ghi
    while (!(I2C1_SR1 & (1 << 1)));  	// Chờ cho đến khi bit ADDR được set
    (void)I2C1_SR2; 					// Đọc thanh ghi SR2 để xóa bit ADDR
}

void I2C_WriteByte(uint8_t data) {
    I2C1_DR = data;						// Ghi byte dữ liệu vào thanh ghi I2C1_DR
    while (!(I2C1_SR1 & (1 << 7)));  	// Chờ cho đến khi bit TXE (Transmit Data Empty) được thiết lập
}

uint8_t I2C_ReadByte(uint8_t ack) {
    if (ack)
        I2C1_CR1 |= (1 << 10);  		// Nếu ack là 1, thiết lập bit ACK trong CR1
    else
        I2C1_CR1 &= ~(1 << 10); 		// Nếu ack là 0, xóa bit ACK trong CR1
    while (!(I2C1_SR1 & (1 << 6)));  	// Chờ cho đến khi dữ liệu được nhận hoàn tất
    return I2C1_DR;						// Trả về byte dữ liệu đã nhận
}

// Hàm đọc hiệu chuẩn
void BMP280_ReadCalibration(void) {
    I2C_Start();                               // Bắt đầu giao tiếp I2C
    I2C_WriteAddress(BMP280_ADDRESS, 0);       // Gửi địa chỉ của BMP280 và chỉ ra rằng chúng ta sẽ ghi (write)
    I2C_WriteByte(BMP280_REG_CALIB);           // Gửi địa chỉ của thanh ghi hiệu chỉnh (calibration register)
    I2C_Start();                               // Bắt đầu một giao tiếp I2C mới (lặp lại I2C_Start để đọc dữ liệu)
    I2C_WriteAddress(BMP280_ADDRESS, 1);       // Gửi địa chỉ của BMP280 và chỉ ra rằng chúng ta sẽ đọc (read)

    // Đọc 3 giá trị hiệu chỉnh (dig_T1, dig_T2, dig_T3)
    dig_T1 = (I2C_ReadByte(1) | (I2C_ReadByte(1) << 8));  // Đọc 2 byte và ghép chúng thành một giá trị 16 bit cho dig_T1
    dig_T2 = (I2C_ReadByte(1) | (I2C_ReadByte(1) << 8));  // Đọc 2 byte và ghép chúng thành một giá trị 16 bit cho dig_T2
    dig_T3 = (I2C_ReadByte(1) | (I2C_ReadByte(1) << 8));  // Đọc 2 byte và ghép chúng thành một giá trị 16 bit cho dig_T3

    I2C_Stop();                                // Dừng giao tiếp I2C
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
    I2C_WriteAddress(BMP280_ADDRESS, 0); // Gửi địa chỉ của BMP280 và yêu cầu ghi (write)
    I2C_WriteByte(BMP280_REG_CTRL_MEAS); // Gửi địa chỉ thanh ghi điều khiển đo lường (Control Measurement Register)
    I2C_WriteByte(0x27);       // Gửi giá trị cấu hình để bắt đầu đo nhiệt độ và áp suất (0x27 là giá trị cấu hình)
    I2C_Stop();                // Dừng giao tiếp I2C
}

// Hàm đọc nhiệt độ thực
void BMP280_ReadTemperature(void) {
    uint8_t msb, lsb, xlsb;
    int32_t adc_T;

    I2C_Start();                                   // Bắt đầu giao tiếp I2C
    I2C_WriteAddress(BMP280_ADDRESS, 0);           // Gửi địa chỉ của BMP280 và yêu cầu ghi
    I2C_WriteByte(BMP280_REG_TEMP_MSB);            // Gửi địa chỉ thanh ghi chứa byte nhiệt độ MSB (Most Significant Byte)

    I2C_Start();                                   // Bắt đầu giao tiếp I2C mới để đọc
    I2C_WriteAddress(BMP280_ADDRESS, 1);           // Gửi địa chỉ của BMP280 và yêu cầu đọc
    msb = I2C_ReadByte(1);                         // Đọc byte MSB của giá trị nhiệt độ
    lsb = I2C_ReadByte(1);                         // Đọc byte LSB của giá trị nhiệt độ
    xlsb = I2C_ReadByte(0);                        // Đọc byte XLSB của giá trị nhiệt độ
    I2C_Stop();                                    // Dừng giao tiếp I2C

    // Ghép ba byte dữ liệu MSB, LSB và XLSB thành một giá trị 20-bit
    adc_T = ((int32_t)msb << 12) | ((int32_t)lsb << 4) | ((int32_t)xlsb >> 4);

    // Bù nhiệt độ và tính toán giá trị nhiệt độ thực tế
    temperature = bmp280_compensate_T_int32(adc_T) / 100.0;
}

int main(void) {
    I2C_Init();
    BMP280_Init();

    while (1) {
        BMP280_ReadTemperature();
        for (volatile int i = 0; i < 1000000; i++);
    }
}
