#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
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



//Crendenciais de wi-fi
#define WIFI_SSID "TEMPLATE"
#define WIFI_PASS "TEMPLATE"
//----------------------------------------------------

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

//variáveis importantes
float temperatura_final = 0;  //médias das duas temperaturas lidas
int32_t pressao = 0;
float umidade = 0;
float max_umidade = 0;
float min_umidade = 0;
float max_temperatura = 0;
float min_temperatura = 0;
float offset_pressão = 0;
float offset_temperatura = 0;


//Configurações para a interface WEB --------------------------------------------------
bool wifi_connection = false;  //armazena status do wi-fi
const char HTML_BODY[] = //página formatada
    "";

struct http_state
{
    char response[4096];
    size_t len;
    size_t sent;
};

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
};

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    //ações do código


    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
};

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}



// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define Button_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
};

void Niveis_Matriz();
void display_dados();

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

    //Conexão wi-fi
    if (cyw43_arch_init())
    {
        printf("Falha ao inicializar wi-fi");
        wifi_connection = 0;
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        printf("Falha ao conectar wi-fi");
        wifi_connection = 0;
        return 1;
    }
    //wi-fi conectado com sucesso
    printf("Falha ao conectar wi-fi");
    wifi_connection = 1;

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

    //Strings para mostrar valores no display

    while (true) {
        
        //leitura do bmp280
        bmp280_read_raw(I2C_PORT_SENSOR, &temperatura_bmp, &pressao_bmp);
        int32_t convert_temperatura_bmp = bmp280_convert_temp(temperatura_bmp, &params);
        pressao = bmp280_convert_pressure(pressao_bmp, temperatura_bmp, &params);

        printf("Pressao = %.3f kPa\n", pressao / 1000.0);
        printf("Temperatura BMP: = %.2f C\n", convert_temperatura_bmp/ 100.0);

        //leitura do aht20
        if (aht20_read(I2C_PORT_SENSOR, &leituraAHT20)) //tirar essa depois depois que testar
        {
            printf("Temperatura AHT: %.2f C\n", leituraAHT20.temperature);
            printf("Umidade: %.2f %%\n\n\n", leituraAHT20.humidity);
            umidade = leituraAHT20.humidity;
        }
        else
        {
            printf("Erro na leitura do AHT10!\n\n\n");
        };
        //Calcula a média das temperaturas
        temperatura_final = (convert_temperatura_bmp + leituraAHT20.temperature)/2;
        //printf("Pressão : %d \n", convert_pressao_bmp);
        //função para o display

        //função para a matriz
        Niveis_Matriz(); //trste nivel umidade = 3, teste temperatura = 5
        //display_dados (temperatura_final, 20, 40, 0, 45, 15); //colocar parâmetros depois --Usra variáveis globais
        sleep_ms(200);
    }
};


//função para matriz -- ok
void Niveis_Matriz(){

    //umidade verde
    //temperatura vermelho
    float variacao_umidade = max_umidade - min_umidade;
    float umidade_atual = (umidade/variacao_umidade)*100;

    float variacao_temp = max_temperatura - min_temperatura;
    float temperatura_atual = (temperatura_final/variacao_temp)*100;

    //arrendondar para o múltiplo de 20 mais próximo
    uint niveis_umidade = umidade_atual/20;
    uint resto = fmod(umidade_atual, 20);
    if (resto >=5){
        niveis_umidade = niveis_umidade+1;
    };

    uint niveis_temp = temperatura_atual/20;
    resto = fmod(temperatura_atual, 20);
    if (resto >=5){
        niveis_temp +=1;
    };

    printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 

     // Garante que não ultrapasse 5
    if (niveis_umidade > 5) niveis_umidade = 5;
    if (niveis_temp > 5) niveis_temp = 5;

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
            if(j ==1 && niveis_umidade>0){
                    niveis_dados[i][j] = cor_umidade;    
            }else if (j ==3 && niveis_temp>0){
                niveis_dados[i][j] = cor_temperatura;
            }else{
                niveis_dados[i][j] = off;
            };
        };
        printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 
        if (niveis_umidade > 0) niveis_umidade --;
        if (niveis_temp > 0) niveis_temp --;
    };
    acender_leds(niveis_dados);
};

//float temperatura, float umidade, float pressao, float max_umidade, float min_umidade, float max_temp, float min_temp
void display_dados (){

    //strings para mostrar dados - 7 ao todo
    char str_pressao[5]; //para a pressão
    int pressaok = pressao/1000;
    sprintf(str_pressao,"%d",pressaok);
    char str_temp[5];
    sprintf(str_temp,"%.2f",temperatura_final);
    char str_umi[5];
    sprintf(str_umi,"%.2f",umidade);
    char str_min_umi[5];
    sprintf(str_min_umi,"%.2f",min_umidade);
    char str_max_umi[5];
    sprintf(str_max_umi,"%.2f",max_umidade);
    char str_min_temp[5];
    sprintf(str_min_temp,"%.2f",min_temperatura);
    char str_max_temp[5];
    sprintf(str_max_temp,"%.2f",max_temperatura);

    //  Atualiza o conteúdo do display com as informações necessárias
    ssd1306_fill(&ssd, !cor);                          // Limpa o display
    ssd1306_rect(&ssd, 3, 1, 122, 61, cor, !cor);      // Desenha um retângulo ok
    ssd1306_line(&ssd, 3, 13, 123, 13, cor);           // Desenha uma linha ok
    ssd1306_line(&ssd, 3, 23, 123, 23, cor);           // Desenha uma linha
    ssd1306_line(&ssd, 3, 33, 123, 33, cor);           // Desenha uma linha
    ssd1306_line(&ssd, 3, 43, 123, 43, cor);           // Desenha uma linha
    ssd1306_line(&ssd, 3, 53, 123, 53, cor);           // Desenha uma linha
    ssd1306_draw_string(&ssd, "WI-FI:", 22, 5); 
    if(wifi_connection)                         //Mostra status de conexão
        ssd1306_draw_string(&ssd, "ON", 50, 5); 
    else
        ssd1306_draw_string(&ssd, "OFF", 50, 5); // Desenha uma string
    //dados de umidade
    ssd1306_draw_string(&ssd, "Umi.:", 10, 15);  
    ssd1306_draw_string(&ssd, str_umi, 60, 15);  
    ssd1306_draw_string(&ssd, "%", 100, 15);  
    ssd1306_draw_string(&ssd, "Mx:", 4, 25);  
    ssd1306_draw_string(&ssd, str_max_umi, 27, 25);
    ssd1306_draw_string(&ssd, "Mn:", 64, 25); 
    ssd1306_draw_string(&ssd, str_min_umi, 89, 25);
    //dados de temperatura
    ssd1306_draw_string(&ssd, "Temp.:", 10, 35); 
    ssd1306_draw_string(&ssd, str_temp, 60, 35);  
    ssd1306_draw_string(&ssd, "C", 100, 35);  
    ssd1306_draw_string(&ssd, "Mx:", 4, 45);   
    ssd1306_draw_string(&ssd, str_max_temp, 27, 45);
    ssd1306_draw_string(&ssd, "Mn:", 64, 45);  
    ssd1306_draw_string(&ssd, str_min_temp, 89, 45);
    //dados de pressão
    ssd1306_draw_string(&ssd, "Pres.:", 10, 55); 
    ssd1306_draw_string(&ssd, str_pressao, 60, 55);  
    ssd1306_draw_string(&ssd, "kPa", 108, 55);  
    
    ssd1306_line(&ssd, 62, 23, 62, 33, cor);           // Desenha uma linha vertical umidade
    ssd1306_line(&ssd, 62, 43, 62, 53, cor);           // Desenha uma linha vertical temperatura
    ssd1306_send_data(&ssd);                           // Atualiza o display
    sleep_ms(700);

};