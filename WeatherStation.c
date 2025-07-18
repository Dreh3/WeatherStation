#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "aht20.h"
#include "bmp280.h"
#include <math.h>
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/matriz.h"

//definindo pinos 
//Sensores
#define I2C_PORT_SENSOR i2c0               // i2c0 pinos 0 e 1, i2c1 pinos 2 e 3
#define I2C_SDA_SENSOR 0                   // 0 ou 2
#define I2C_SCL_SENSOR 1                   // 1 ou 3
// Display
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C
bool cor = true;
ssd1306_t ssd;                                                     // Inicializa a estrutura do display

//Configurações da matriz
PIO pio = pio0; 
uint sm = 0;  

// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define Button_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
};

void Niveis_Matriz(float temperatura, float umidade, float max_umidade, float min_umidade, float max_temp, float min_temp);

int main()
{
    stdio_init_all();

    //inicializar pinos 
    // Botão B --------------------------------------------------------------------------------------------
    gpio_init(Button_B);
    gpio_set_dir(Button_B, GPIO_IN);
    gpio_pull_up(Button_B);
    gpio_set_irq_enabled_with_callback(Button_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    //Display ---------------------------------------------------------------------------------------------
    // I2C do Display funcionando em 400Khz 
    i2c_init(I2C_PORT_DISP, 400 * 1000);

    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA_DISP);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL_DISP);                                        // Pull up the clock line
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP); // Inicializa o display
    ssd1306_config(&ssd);                                              // Configura o display
    ssd1306_send_data(&ssd);                                           // Envia os dados para o display
    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    //Pinos para i2c dos sensores ---------------------------
    i2c_init(I2C_PORT_SENSOR, 400 * 1000);
    gpio_set_function(I2C_SDA_SENSOR, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSOR, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSOR);
    gpio_pull_up(I2C_SCL_SENSOR);

    //Configurações para matriz de leds ---------------------
    uint offset = pio_add_program(pio, &pio_matrix_program);
    pio_matrix_program_init(pio, sm, offset, MatrizLeds, 800000, IS_RGBW);

    //Inicializando sensores---------------------------------

    //BMP280
    bmp280_init(I2C_PORT_SENSOR);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT_SENSOR, &params);
    //AHT20
     aht20_reset(I2C_PORT_SENSOR);
    aht20_init(I2C_PORT_SENSOR);
    //--------------------------------------------------------
    //Variáveis para os dados dos sensores
    // Estrutura para armazenar os dados do sensor
    AHT20_Data leituraAHT20;
    int32_t temperatura_bmp;
    int32_t pressao_bmp;

    float temperatura_final;  //médias das duas temperaturas lidas

    //Strings para mostrar valores no display

    while (true) {
        
        //leitura do bmp280
        bmp280_read_raw(I2C_PORT_SENSOR, &temperatura_bmp, &pressao_bmp);
        int32_t convert_temperatura_bmp = bmp280_convert_temp(temperatura_bmp, &params);
        int32_t convert_pressao_bmp = bmp280_convert_pressure(pressao_bmp, temperatura_bmp, &params);

        //leitura do aht20
        if (aht20_read(I2C_PORT_SENSOR, &leituraAHT20)) //tirar essa depois depois que testar
        {
            printf("Temperatura AHT: %.2f C\n", leituraAHT20.temperature);
            printf("Umidade: %.2f %%\n\n\n", leituraAHT20.humidity);
        }
        else
        {
            printf("Erro na leitura do AHT10!\n\n\n");
        };
        //Calcula a temperatura
        temperatura_final = (convert_temperatura_bmp + leituraAHT20.temperature)/2;

        //função para o display

        //função para a matriz
        Niveis_Matriz(0, 0, 40, 0, 45, 15); //trste nivel umidade = 3, teste temperatura = 5

    }
};


//função para o display

//função para matriz 
void Niveis_Matriz(float temperatura, float umidade, float max_umidade, float min_umidade, float max_temp, float min_temp){

    //umidade verde
    //temperatura vermelho
    float variacao_umidade = max_umidade - min_umidade;
    float umidade_atual = (umidade/variacao_umidade)*100;

    float variacao_temp = max_temp - min_temp;
    float temperatura_atual = (temperatura/variacao_temp)*100;

    //arrendondar para o múltiplo de 20 mais próximo
    uint niveis_umidade = umidade_atual/20;
    uint resto = fmod(umidade_atual, 20);
    if (resto >=5){
        niveis_umidade = niveis_umidade+1;
    };

    uint niveis_temp = temperatura_atual/20;
    resto = fmod(temperatura_atual, 20);
    if (resto >=5){
        niveis_temp =+1;
    };

    printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 

    //Padrão para desligar led da matriz
    COR_RGB off = {0.0,0.0,0.0};
    //Vetor para variar entre os estados da matriz
    COR_RGB cor_umidade = {0.004,0.004,0.001};
    COR_RGB cor_temperatura = {0.008,0.002,0.001};

    //matriz que será modificada no loop
    Matriz_leds niveis_dados = {{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off}};
    //acender_leds(niveis_dados);
    for(int i = 4; i>=0; i --){
       
        for(int j = 0; j< 5; j++){
            if(j ==1){
                if (niveis_umidade>0)
                    niveis_dados[i][j] = cor_umidade;
                else
                    niveis_dados[i][j] = off;    
            }else if (j ==3){
                if (niveis_temp>0)
                    niveis_dados[i][j] = cor_temperatura;
                else
                    niveis_dados[i][j] = off;
            }else{
                niveis_dados[i][j] = off;
            };
        };
        printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 
        niveis_temp = niveis_temp - 1;
        niveis_umidade = niveis_umidade - 1;
    };
    acender_leds(niveis_dados);
};