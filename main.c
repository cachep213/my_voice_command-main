#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "stdlib.h"
#include "cyberon_asr.h"

#define FRAME_SIZE                  (480u)
#define SAMPLE_RATE_HZ              (16000u)
#define DECIMATION_RATE             (96u)
#define AUDIO_SYS_CLOCK_HZ          (24576000u)
#define PDM_DATA                    (P10_5)
#define PDM_CLK                     (P10_4)
#define LED_PIN						(P13_7)
#define LED_FREQ					(850u)
//defin pin driver motor, musis
#define DRIVE_1_PIN						(P12_0)
#define DRIVE_2_PIN						(P12_1)

#define MUSIC_PIN						(P12_5)
#define LIGHT_PIN						(P11_6)

cyhal_pwm_t pwm_obj;
float duty_cycle = 100.f;

void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event);
void clock_init(void);

void asr_callback(const char *function, char *message, char *parameter);

void command_exec(uint32_t cmd_id);

volatile bool pdm_pcm_flag = false;
int16_t pdm_pcm_ping[FRAME_SIZE] = {0};
int16_t pdm_pcm_pong[FRAME_SIZE] = {0};
int16_t *pdm_pcm_buffer = &pdm_pcm_ping[0];
cyhal_pdm_pcm_t pdm_pcm;
cyhal_clock_t   audio_clock;
cyhal_clock_t   pll_clock;

const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg =
{
    .sample_rate     = SAMPLE_RATE_HZ,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_LEFT,
    .word_length     = 16,  /* bits */
    .left_gain       = CYHAL_PDM_PCM_MAX_GAIN,   /* dB */
    .right_gain      = CYHAL_PDM_PCM_MAX_GAIN,   /* dB */
};

int main(void)
{
    cy_rslt_t result;
    uint64_t uid;

    result = cybsp_init() ;
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq();
    clock_init();
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    cyhal_pwm_init(&pwm_obj, LED_PIN, NULL);
    cyhal_pwm_set_duty_cycle(&pwm_obj, duty_cycle, LED_FREQ);
    cyhal_pwm_start(&pwm_obj);

    cyhal_pdm_pcm_init(&pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_pcm_isr_handler, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);
    cyhal_pdm_pcm_start(&pdm_pcm);
    cyhal_pdm_pcm_read_async(&pdm_pcm, pdm_pcm_buffer, FRAME_SIZE);


    result = cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT,
                      CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    result = cyhal_gpio_init(DRIVE_1_PIN, CYHAL_GPIO_DIR_OUTPUT,
                      CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    result = cyhal_gpio_init(DRIVE_2_PIN, CYHAL_GPIO_DIR_OUTPUT,
                      CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    result = cyhal_gpio_init(MUSIC_PIN, CYHAL_GPIO_DIR_OUTPUT,
                      CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    result = cyhal_gpio_init(LIGHT_PIN, CYHAL_GPIO_DIR_OUTPUT,
                      CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);
     /* User LED init failed. Stop program execution */
     //handle_error(result);


    printf("\x1b[2J\x1b[;H");
    printf("===== Voice command Demo =====\r\n");

    uid = Cy_SysLib_GetUniqueId();
    printf("uniqueIdHi: 0x%08lX, uniqueIdLo: 0x%08lX\r\n", (uint32_t)(uid >> 32), (uint32_t)(uid << 32 >> 32));
    if(!cyberon_asr_init(asr_callback))
    {
    	while(1);
    }
    printf("\r\nAwaiting voice input trigger command (\"Hi Infinion !\"):\r\n");
    float last_dc= duty_cycle;
    while(1)
    {

    	//write command function here
    	if( last_dc != duty_cycle )
    	{
    		cyhal_pwm_set_duty_cycle(&pwm_obj, duty_cycle, LED_FREQ);
			cyhal_pwm_start(&pwm_obj);
			last_dc= duty_cycle;
    	}

        if(pdm_pcm_flag)
        {
            pdm_pcm_flag = 0;
            cyberon_asr_process(pdm_pcm_buffer, FRAME_SIZE);
        }
    }
}

void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event)
{
    static bool ping_pong = false;

    (void) arg;
    (void) event;

    if(ping_pong)
    {
        cyhal_pdm_pcm_read_async(&pdm_pcm, pdm_pcm_ping, FRAME_SIZE);
        pdm_pcm_buffer = &pdm_pcm_pong[0];
    }
    else
    {
        cyhal_pdm_pcm_read_async(&pdm_pcm, pdm_pcm_pong, FRAME_SIZE);
        pdm_pcm_buffer = &pdm_pcm_ping[0];
    }

    ping_pong = !ping_pong;
    pdm_pcm_flag = true;
}

void clock_init(void)
{
	cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[0]);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    cyhal_clock_set_enabled(&pll_clock, true, true);

    cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);

    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);
}


void asr_callback(const char *function, char *message, char *parameter)
{
	printf("[%s]%s(%s)\r\n", function, message, parameter);
	int id_cmd = atoi(parameter);
	command_exec(id_cmd);
}

void command_exec(uint32_t cmd_id)
{
	switch(cmd_id)
	{
		case 1: // chair up
			cyhal_gpio_write(DRIVE_1_PIN, CYBSP_LED_STATE_OFF);
			cyhal_gpio_write(DRIVE_2_PIN, CYBSP_LED_STATE_ON);
			//duty_cycle = 0.f;
			break;
		case 2: // chair down
			cyhal_gpio_write(DRIVE_1_PIN, CYBSP_LED_STATE_ON);
			cyhal_gpio_write(DRIVE_2_PIN, CYBSP_LED_STATE_OFF);
			//duty_cycle = 100.f;
			break;
		case 3: // chair stop
			cyhal_gpio_write(DRIVE_1_PIN, CYBSP_LED_STATE_OFF);
			cyhal_gpio_write(DRIVE_2_PIN, CYBSP_LED_STATE_OFF);
			//duty_cycle = 50.f;
			break;
		case 4: // play music
			cyhal_gpio_write(MUSIC_PIN, CYBSP_LED_STATE_ON);

			//duty_cycle = 25.f;
			break;
		case 5: // stop music
			cyhal_gpio_write(MUSIC_PIN, CYBSP_LED_STATE_OFF);
			//duty_cycle = 70.f;
			break;
		case 6: // Light on
			cyhal_gpio_write(LIGHT_PIN, CYBSP_LED_STATE_ON);

			//duty_cycle = 0.f;
			break;
		case 7: // light off
			cyhal_gpio_write(LIGHT_PIN, CYBSP_LED_STATE_OFF);
			//duty_cycle = 100.f;
			break;
		default :
			break;
	};
}
