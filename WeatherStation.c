//Código desenvolvido por Andressa Sousa Fonseca

#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "aht20.h"
#include "bmp280.h"
#include <math.h>
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/matriz.h"
#include "lib/pages.h"


//Crendenciais de wi-fi
#define WIFI_SSID "TEMPLATE"
#define WIFI_PASS "TEMPLATE"
//----------------------------------------------------

//definindo pinos 
//leds
#define led_green 11
#define led_blue 12
#define led_red 13
//buzzer
//Pino do buzzer
#define Buzzer 21
#define WRAP 65535
uint sons_freq[] = {880,784,740,659};   //Frequência para os sons de alerta
uint slice;
pwm_config config2;
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
float temperatura_final = 30;  //médias das duas temperaturas lidas
int32_t pressao = 0;
float umidade = 0;
float max_umidade = 0;
float min_umidade = 0;
float max_temperatura = 0;
float min_temperatura = 0;
float min_pressao = 0;
float max_pressao = 0;
float offset_pressao = 0;
float offset_umidade = 0;
float offset_temperatura = 0; 

struct http_state
{
    char response[4700];
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
    if (strstr(req, "GET /dados"))
    {
        
        char json_payload[300];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"temp\":%.2f,\"hum\":%.2f,\"press\":%d,\"tempMin\":%.2f,\"tempMax\":%.2f,\"tempOffset\":%.2f,\"humMin\":%.2f,\"humMax\":%.2f,\"humOffset\":%.2f,\"pressMin\":%.2f,\"pressMax\":%.2f,\"pressOffset\":%.2f}\r\n",
                                temperatura_final,umidade, pressao, min_temperatura, max_temperatura, offset_temperatura, min_umidade, max_umidade, offset_umidade, min_pressao, max_pressao, offset_pressao);

        printf("[DEBUG] JSON: %s\n", json_payload);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    else if (strstr(req, "GET /graficos") != NULL) {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(graficos_html), graficos_html);

    
    }

   else if (strstr(req, "POST /config")) {

    char *read_data = strstr(req, "\r\n\r\n");
    if (read_data) {
        read_data += 4;
        int lidos = sscanf(read_data,
            "{\"tempMin\" :%f,\"tempMax\" :%f,\"tempOffset\" :%f,"
            "\"humMin\":%f,\"humMax\":%f,\"humOffset\":%f,"
            "\"pressMin\":%f,\"pressMax\":%f,\"pressOffset\":%f}",
            &min_temperatura, &max_temperatura, &offset_temperatura,
            &min_umidade, &max_umidade, &offset_umidade,
            &min_pressao, &max_pressao, &offset_pressao);

        if (lidos == 9) {
            printf(" POST recebido com sucesso:\n");
            //um alerta sonoro 
            for(int i =0; i<4; i++){                //Laço de repetição para modificar as frequência do som de alerta
                pwm_config_set_clkdiv(&config2, clock_get_hz(clk_sys) / (sons_freq[i] * WRAP));
                pwm_init(slice, &config2, true);
                pwm_set_gpio_level(Buzzer, WRAP/2); //Level metade do wrap para volume médio
                sleep_ms(100);     //pausa entre sons
            };
            pwm_set_gpio_level(Buzzer, 0);         //desliga o buzzer
        } else {
            printf(" Erro ao interpretar JSON no POST! Campos reconhecidos: %d\n", lidos);
            printf("Conteúdo recebido: %s\n", read_data);
        }
        
    }
    // Cria resposta
    char json_payload[200];
    int json_len = snprintf(json_payload, sizeof(json_payload),
        "{\"tempMin\":%.2f,\"tempMax\":%.2f,\"tempOffset\":%.2f,"
        "\"humMin\":%.2f,\"humMax\":%.2f,\"humOffset\":%.2f,"
        "\"pressMin\":%d,\"pressMax\":%d,\"pressOffset\":%d}",
        min_temperatura, max_temperatura, offset_temperatura,
        min_umidade, max_umidade, offset_umidade,
        min_pressao, max_pressao, offset_pressao);

    hs->len = snprintf(hs->response, sizeof(hs->response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        json_len, json_payload);
    }
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }


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

//protótipo das funções
void Niveis_Matriz();
void display_dados();
void liga_led(uint pin);

int main()
{
    stdio_init_all();

    //inicializar pinos 
    // Botão B --------------------------------------------------------------------------------------------
    gpio_init(Button_B);
    gpio_set_dir(Button_B, GPIO_IN);
    gpio_pull_up(Button_B);
    gpio_set_irq_enabled_with_callback(Button_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    
    //leds ------------------------------------------------------------------------------------------------
    gpio_init(led_blue);
    gpio_init(led_red);
    gpio_init(led_green);
    gpio_set_dir(led_blue, GPIO_OUT);
    gpio_set_dir(led_red, GPIO_OUT);
    gpio_set_dir(led_green, GPIO_OUT);
    gpio_put(led_blue, 0);
    gpio_put(led_red, 0);
    gpio_put(led_green, 0);

    //buzzer-----------------------------------------------------------------------------------------------
    //Configurações de PWM
    gpio_set_function(Buzzer, GPIO_FUNC_PWM);   //Configura pino do led como pwm
    slice = pwm_gpio_to_slice_num(Buzzer);      //Adiquire o slice do pino
    pwm_config config = pwm_get_default_config();
    config2 = config;
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (4400 * WRAP));
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(Buzzer, 0);              //Determina com o level desejado - Inicialmente desligado


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
    printf("wi-fi conectado");
    wifi_connection = 1;

     // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

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

    //permite carregar a página no servidor
    start_http_server();

    while (true) {
        
        cyw43_arch_poll(); 

        //leitura do bmp280
        bmp280_read_raw(I2C_PORT_SENSOR, &temperatura_bmp, &pressao_bmp);
        int32_t convert_temperatura_bmp = bmp280_convert_temp(temperatura_bmp, &params);
        pressao = ((bmp280_convert_pressure(pressao_bmp, temperatura_bmp, &params))/1000) + offset_pressao;

        //printf("Pressao = %.3f kPa\n", pressao);
        //printf("Temperatura BMP: = %.2f C\n", convert_temperatura_bmp/ 100.0);

        //leitura do aht20
        if (aht20_read(I2C_PORT_SENSOR, &leituraAHT20)) //tirar essa depois depois que testar
        {
            //printf("Temperatura AHT: %.2f C\n", leituraAHT20.temperature);
            //printf("Umidade: %.2f %%\n\n\n", leituraAHT20.humidity);
            umidade = leituraAHT20.humidity + offset_umidade;
        }
        else
        {
            printf("\nErro na leitura do AHT10!\n");
        };
        //Calcula a média das temperaturas
        temperatura_final = ((convert_temperatura_bmp/100 + leituraAHT20.temperature)/2) + offset_temperatura;
        //printf("Pressão : %d \n", convert_pressao_bmp);
        
        //leds
        if(temperatura_final>=max_temperatura && pressao >= max_pressao && umidade>= max_umidade){
            liga_led(led_red);
        }else if (temperatura_final<max_temperatura && pressao < max_pressao && umidade< max_umidade){
            liga_led(led_green);
        }else{
            liga_led(led_blue);
        };

        //função para a matriz
        Niveis_Matriz(); 
        //função para o display
        display_dados(); 
        sleep_ms(200);
    }
    // Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
};

//função para leds
void liga_led(uint pin){
    gpio_put(pin, 1);
    if(pin==13){
        gpio_put(led_blue, 0);
        gpio_put(led_green, 0);
    }else if(pin ==12){
        gpio_put(led_green, 0);
        gpio_put(led_red, 0);
    }else{
        gpio_put(led_blue, 0);
        gpio_put(led_red, 0);
    };
};

//função para matriz
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

    //printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 

     // Garante que não ultrapasse 5
    if (niveis_umidade > 5) niveis_umidade = 5;
    if (niveis_temp > 5) niveis_temp = 5;

    //Padrão para desligar led da matriz
    COR_RGB off = {0.0,0.0,0.0};
    //parâmetros para cores
    COR_RGB cor_umidade = {0.004,0.004,0.001};
    COR_RGB cor_temperatura = {0.008,0.002,0.001};

    //matriz que será modificada no loop
    Matriz_leds niveis_dados = {{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off},{off,off,off,off,off}};
    
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
        //printf("nível umidade = %d nivel temperatura = %d\n",niveis_umidade, niveis_temp); //ok até aqui 
        if (niveis_umidade > 0) niveis_umidade --;
        if (niveis_temp > 0) niveis_temp --;
    };
    acender_leds(niveis_dados);
};

//função para exibir dados no display
void display_dados (){

    //strings para mostrar dados - 7 ao todo
    char str_pressao[5]; //para a pressão
    int pressaok = pressao;
    sprintf(str_pressao,"%d",pressaok);
    char str_temp[5];
    sprintf(str_temp,"%.2f",temperatura_final);
    char str_umi[5];
    sprintf(str_umi,"%.2f",umidade);
    char str_min_umi[5];
    sprintf(str_min_umi,"%.0f",min_umidade);
    char str_max_umi[5];
    sprintf(str_max_umi,"%.0f",max_umidade);
    char str_min_temp[5];
    sprintf(str_min_temp,"%.0f",min_temperatura);
    char str_max_temp[5];
    sprintf(str_max_temp,"%.0f",max_temperatura);

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
        ssd1306_draw_string(&ssd, "ON", 78, 5); 
    else
        ssd1306_draw_string(&ssd, "OFF", 78, 5); // Desenha uma string
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
    ssd1306_draw_string(&ssd, "kPa", 90, 55);  
    
    ssd1306_line(&ssd, 62, 23, 62, 33, cor);           // Desenha uma linha vertical umidade
    ssd1306_line(&ssd, 62, 43, 62, 53, cor);           // Desenha uma linha vertical temperatura
    ssd1306_send_data(&ssd);                           // Atualiza o display
    sleep_ms(700);

};