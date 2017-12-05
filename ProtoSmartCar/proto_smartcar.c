/*
 * proto_smartcar.c
 *
 *  Created on: 2017. 11. 24.
 *      Author: holinder4s
 */

#include <misc.h>
#include <stm32f10x.h>
#include <stm32f10x_gpio.h>
#include <stm32f10x_rcc.h>
#include <stm32f10x_usart.h>
#include <stm32f10x_adc.h>
#include <stm32f10x_tim.h>
#include <stm32f10x_dma.h>
#include <string.h>
#include <lcd.h>
#include <Touch.h>
#include <SHT15.h>
#include <MPU6050.h>
#include <stdbool.h>
#include <math.h>

#include "usart.h"
#include "DHT11.h"
#include "delay.h"
//#include <Tick.h>

/* Globl Variables
 *    1) ADC_result_value_arr[2] : [0](��������), [1](������������), [2](��ü��������)
 *    2) command[100] : ��ɾ� ����
 *    3) command_pos : ��ɾ� ��ġ �ʱ�ȭ �뵵
 *    4) voiceBuffer : ���̽� ��ɾ� ���ڿ� ��
 *    5) rain_power_flag : ���� ����
 *    6) servo_direction : �����۸� �ֱ������� �պ���Ű�� ���� ���� �÷���
 *    7) timer_count : ���� ���⿡ ���� �������� �ӵ��� �����ϱ� ���� timer count
 *    8) voice_command_enable : ���� ��� ��� Ȱ��ȭ �÷���
 *    9) AccelGyro[6] : ���ӵ� ���� Raw ������
 *    10) AccelGyroAverage_flag : true�̸� average��� ���(�ʱ�ȭ), false�̸� ���� ����
 *    11) base_???_? : ���ӵ� ���� Raw ������ ��� ��
 *    12) last_???_??? : �ֽ� ���ӵ� ���� ���� ��
 *    13) g_currentTick : TickCount�� => �ð� ����� ���� �̿�(1msec)*/
__IO uint32_t ADC_result_value_arr[3];
char command[100];
int command_pos=0;

u8 *voiceBuffer[] = {"Turn on the light", "Turn off the light", "Play music", "Pause", "Next", "Previous",
		"Up", "Down", "Turn on the TV", "Turn off the TV", "Increase temperature", "Decrease temperature",
		"What's the time", "Open the door", "Close the door", "Left", "Right", "Stop", "Start", "Mode 1", "Mode 2", "Go",};

int rain_power_flag = 0;
int servo_direction = 0;
int timer_count = 0;
int voice_command_enable = 0;

/* wjdebug : ���ӵ� ���� �׽�Ʈ */
int16_t  AccelGyro[6]={0};
bool AccelGyroAverage_flag = true;
uint16_t base_accel_x=0, base_accel_y=0, base_accel_z=0;
uint16_t base_gyro_x=0, base_gyro_y=0, base_gyro_z=0;
unsigned long last_read_time;
float last_angle_x, last_angle_y, last_angle_z;
float last_gyro_angle_x, last_gyro_angle_y, last_gyro_angle_z;

/* ���� Tick Count���� �����ϴ� �������� */
uint32_t g_currentTick = 0;

int speed_updown_timer_count = 0;

/* DHT11 �½��� ���� */
u8 temperature;
u8 humidity;

void RCC_Configure(void);
void USART_Configure(void);
void _GPIO_LEDInit(void);
void _GPIO_MOTOR1Init(void);
void _GPIO_MOTOR2Init(void);
void _GPIO_MOTORInit(void);
void _GPIO_ADCInit(void);
void _GPIO_TIM3Ch3Ch1Init(void);
void GPIO_Configure(void);
void ADC_Configure(void);
void ADC_Initialize(void);
void _TIM3Ch3Ch1_PWM_Configure(void);
void _TIM2_Configure(void);
void TIM_PWM_Configure(void);
void DMA_Configure(void);
void _USART_ITR_Configure(void);
void _TIM2_ITR_Configure(void);
void Interrupt_Configure(void);
void SendString(USART_TypeDef* USARTx, char* string);
void _command_forward(void);
void _command_backward(void);
void _command_left(void);
void _command_right(void);
void _command_stop(void);
void command_move(int select);
void change_pwm_servo_duty_cycle(int percentx10);
void speed_up_down(int percentx10);
void AccelGyroInitialize(void);
void calculate_accelgyro_average(int total_readnum);
void set_last_read_angle_data(unsigned long time, float x, float y, float z, float gyro_x, float gyro_y, float gyro_z);
void PrintAccelGryroRaw2Angle(int16_t accelgyro[6]);
uint32_t GetTickCount(void);
void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void UIOutline_Init(void);


int main(void) {
	/* SHT15 �½������� Variables */
	uint16_t humi_val, temp_val;
	uint8_t err = 0, checksum = 0;
	float humi_val_real = 0.0;
	float temp_val_real = 0.0;
	float dew_point = 0.0;

	/* �ʱ�ȭ �� Configuration �۾� */
	RCC_Configure();
	LCD_Init();
	USART_Configure();
	GPIO_Configure();
	ADC_Configure();
	TIM_PWM_Configure();
	DMA_Configure();
	MPU6050_I2C_Init();			// MPU6050�� ����ϱ� ���� I2C ����
	Interrupt_Configure();

	ADC_Initialize();

	/* SHT15 �ʱ�ȭ */
	SHT15_Init();

	/* I2C�� �̿��� MPU6050 �ʱ�ȭ */
	MPU6050_Initialize();

	/* ���� UIƲ �ʱ�ȭ */
	LCD_Clear(BLACK);
	UIOutline_Init();
	//LCD_ShowString(50, 130, "Accident Occur!!!", RED, WHITE);

	/* Debug�� : ���߿� ������ ��! */
	GPIO_ResetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
	command_move(1);
	while (1) {
		/* �������� �� LCD�� ���
		 *    1) x > 3000 : ��(��ο�)
		 *    2) else : ��(����) */
		//LCD_ShowNum(20,40,ADC_result_value_arr[0],10,BLACK,WHITE);
		if(ADC_result_value_arr[0] > 3000) {
			GPIO_SetBits(GPIOD,GPIO_Pin_2);
			GPIO_SetBits(GPIOD,GPIO_Pin_3);
		}
		else {
			GPIO_ResetBits(GPIOD,GPIO_Pin_2);
			GPIO_ResetBits(GPIOD,GPIO_Pin_3);
		}

		/* ���� ���� ���� �� LCD�� ���
		 *    1) x < 3000 : ���� ����
		 *    2) x < 3500 : ���� ����
		 *    3) else : ���� x */
		//LCD_ShowNum(20,60,ADC_result_value_arr[1],10,BLACK,WHITE);
		if(ADC_result_value_arr[1]<3000) {
			rain_power_flag = 2;
			GPIO_ResetBits(GPIOD,GPIO_Pin_3);
		}
		else if(ADC_result_value_arr[1]<3500) {
			rain_power_flag = 1;
			GPIO_ResetBits(GPIOD,GPIO_Pin_4);
		}
		else {
			rain_power_flag = 0;
			GPIO_ResetBits(GPIOD,GPIO_Pin_7);
		}

		/* ��ü ���� ���� �� LCD�� ���
		 *    1) x < 2000 : ��� ����
		 *    2) x > 2000 : ��� ���� */
		//LCD_ShowNum(20, 80, ADC_result_value_arr[2], 10, BLACK, WHITE);
		if(ADC_result_value_arr[2] > 2000) {
			voice_command_enable = 1;
		}else {
			voice_command_enable = 0;
		}

		/* SHT15 �½��� ���� ���
		 *    1) �µ� ���� : float���� temp_val_real�� ����
		 *    2) ���� ���� : float���� humi_val_real�� ����
		 *    3) �̽��� �Ի� : float���� dew_point�� ���� */
		err += SHT15_Measure(&temp_val, &checksum, TEMP);                  // �µ� ����
		err += SHT15_Measure(&humi_val, &checksum, HUMI);                  // ���� ����
		if(err != 0)
			SHT15_ConReset();
		else {
			SHT15_Calculate(temp_val, humi_val, &temp_val_real, &humi_val_real);	// ���� �µ� �� ���� �� ���
			dew_point = SHT15_CalcuDewPoint(temp_val_real, humi_val_real);			// �̽��� �µ� ���
		}
		//LCD_ShowNum(20,100,(int)temp_val_real,10,BLACK,WHITE);
		//LCD_ShowNum(20,120,(int)humi_val_real,10,BLACK,WHITE);
		//LCD_ShowNum(20,140,(int)dew_point,10,BLACK,WHITE);

		/* MPU6050 6�� ���̷� ���ӵ� ���� ���
		 *    1) ��� ���� : raw sensor�����͸� �޾� ���� ���
		 *    2) 20�� �̻� : DC ���� ��� 60%(�ӵ� ���)
		 *    3) 40�� �̻� : DC ���� ��� 70%(�ӵ� ���)
		 *    4) -20�� ~ 20�� : DC ���� ��� 50%(��� �ӵ�)
		 *    5) -20�� ���� : DC ���� ��� 40%(�ӵ� ����)
		 *    6) -40�� ���� : DC ���� ��� 30%(�ӵ� ����) */
		if (MPU6050_TestConnection() != 0) {
			if(AccelGyroAverage_flag == true) {
				AccelGyroInitialize();
				calculate_accelgyro_average(10);
				AccelGyroAverage_flag = false;
			}else {
				/* Sensor�κ��� ������ �޾ƿ��� */
				MPU6050_GetRawAccelGyro(AccelGyro);
				/* ���� raw�����͸� ������ ���� �� ��� */
				PrintAccelGryroRaw2Angle(AccelGyro);

				/* ��簢������ DC���� �ӵ� ��� ���� ���� �ӵ��� ���� */
				/* 1�ʸ��� TIM2 Interrupt Handler���� �ӵ� ���� */
			}
		}

		/* DHT11 �½������� */
	}
}


void RCC_Configure(void) {
	/* Enable Clock for Interrupt EXTI(�ܺ� ���ͷ�Ʈ) */
	//RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO);

	/* Enable Clock for Bluetooth(USART2)(�ȵ���̵�->����) and USART1 and USART3(PortB)(�����νļ���) */
	// GPIOA��Ʈ�� USART�� clock������ ����ؾ��ϴ��� ��� ���ص� �Ǵ��� �� �𸣰���
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2ENR_USART1EN, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1ENR_USART2EN | RCC_APB1ENR_USART3EN, ENABLE);

	/* ���͵���̹�
	 *    1) ���͵���̹�1(in1:PB8,in2:PB9,in3:PB14,in4:PB15)
	 *    2) ���͵���̹�2(in1:PE4,in2:PE5,in3:PE14,in4:PE15)  */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

	/* LED */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

	/* ���������� �������������� ����ϱ� ���� ADC & DMA Ŭ�� �ΰ� */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1,ENABLE);
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1,ENABLE);

	/* Servo Motor�� �����ϱ� ���� TIM3_Channel3(PB0) ��� */
	/* ���� �ӵ� ����(PWM)�� ���� TIM3_Channel1(PA6) ��� */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	/* ������ �۵��� �ֱ� ������ ���� TIM2 ��� */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	/* ���ӵ� GetTickCount()������ �ϴ� �Լ��� ���� TIM1(1msec�ֱ�) Ŭ�� �ΰ� */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
}

void USART_Configure(void) {
	/* UART ��� & BT ���� : USART1 */
	/* Bluetooth module : USART2 */
	USART_InitTypeDef USART_init;

	/* USART �⺻ ���� �� ���� */
	USART_init.USART_BaudRate = 9600;
	USART_init.USART_WordLength = USART_WordLength_8b;
	USART_init.USART_StopBits = USART_StopBits_1;
	USART_init.USART_Parity = USART_Parity_No;
	USART_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_init.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

	/* USART1 Init(���������� ������ ���� ���) */
	/* USART2 Init(������� ������ ���� ���, �ȵ���̵�->����)
	 * USART3 Init(�����νļ��� ���) */
	USART_Init(USART1, &USART_init);
	USART_Init(USART2, &USART_init);
	USART_Init(USART3, &USART_init);

	/* Enable USART1, USART2, USART3 Receive/Transmit interrupts */
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

	/* Enable USART1, USART2, USART3 */
	USART_Cmd(USART1, ENABLE);
	USART_Cmd(USART2, ENABLE);
	USART_Cmd(USART3, ENABLE);
}

void _GPIO_USARTInit(void) {
	GPIO_InitTypeDef GPIO_USART_init;

	/* USART�� input clock�� ���� GPIOA�� �⺻ �� ���� */
	GPIO_USART_init.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_USART_init.GPIO_Speed = GPIO_Speed_50MHz;

	/* GPIOA�� 9�� 10�� ���� Alternative Function���� ���� */
	GPIO_USART_init.GPIO_Pin = GPIO_Pin_9;
	GPIO_Init(GPIOA, &GPIO_USART_init);

	GPIO_USART_init.GPIO_Pin = GPIO_Pin_10;
	GPIO_USART_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_USART_init);

	/* GPIOA�� 2�� 3�� ���� Alternative Function���� ���� */
	GPIO_USART_init.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_USART_init.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_USART_init.GPIO_Pin = GPIO_Pin_2;
	GPIO_Init(GPIOA, &GPIO_USART_init);

	GPIO_USART_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_USART_init.GPIO_Pin = GPIO_Pin_3;
	GPIO_Init(GPIOA, &GPIO_USART_init);

	/* GPIOA�� 2�� 3�� ���� Alternative Function���� ���� */
	GPIO_USART_init.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_USART_init.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_USART_init.GPIO_Pin = GPIO_Pin_10;
	GPIO_Init(GPIOB, &GPIO_USART_init);

	GPIO_USART_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_USART_init.GPIO_Pin = GPIO_Pin_11;
	GPIO_Init(GPIOB, &GPIO_USART_init);
}

void _GPIO_LEDInit(void) {
	GPIO_InitTypeDef GPIOD_LED_init;

	GPIOD_LED_init.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIOD_LED_init.GPIO_Speed = GPIO_Speed_50MHz;

	GPIOD_LED_init.GPIO_Pin = GPIO_Pin_2;
	GPIO_Init(GPIOD, &GPIOD_LED_init);

	GPIOD_LED_init.GPIO_Pin = GPIO_Pin_3;
	GPIO_Init(GPIOD, &GPIOD_LED_init);

	GPIOD_LED_init.GPIO_Pin = GPIO_Pin_4;
	GPIO_Init(GPIOD, &GPIOD_LED_init);

	GPIOD_LED_init.GPIO_Pin = GPIO_Pin_7;
	GPIO_Init(GPIOD, &GPIOD_LED_init);

	/* LED Init State : LED1, LED2, LED3, LED4 on*/
	GPIO_SetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
}

void _GPIO_MOTOR1Init(void) {
	GPIO_InitTypeDef GPIOB_MOTOR1_init;

	GPIOB_MOTOR1_init.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIOB_MOTOR1_init.GPIO_Speed = GPIO_Speed_50MHz;

	GPIOB_MOTOR1_init.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOE, &GPIOB_MOTOR1_init);

	GPIOB_MOTOR1_init.GPIO_Pin = GPIO_Pin_1;
	GPIO_Init(GPIOE, &GPIOB_MOTOR1_init);

	GPIOB_MOTOR1_init.GPIO_Pin = GPIO_Pin_2;
	GPIO_Init(GPIOE, &GPIOB_MOTOR1_init);

	GPIOB_MOTOR1_init.GPIO_Pin = GPIO_Pin_3;
	GPIO_Init(GPIOE, &GPIOB_MOTOR1_init);

	/* ���� ���� ���� �ʱ�ȭ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3);
}

void _GPIO_MOTOR2Init(void) {
	GPIO_InitTypeDef GPIOE_MOTOR2_init;

	GPIOE_MOTOR2_init.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIOE_MOTOR2_init.GPIO_Speed = GPIO_Speed_50MHz;

	GPIOE_MOTOR2_init.GPIO_Pin = GPIO_Pin_4;
	GPIO_Init(GPIOE, &GPIOE_MOTOR2_init);

	GPIOE_MOTOR2_init.GPIO_Pin = GPIO_Pin_5;
	GPIO_Init(GPIOE, &GPIOE_MOTOR2_init);

	GPIOE_MOTOR2_init.GPIO_Pin = GPIO_Pin_14;
	GPIO_Init(GPIOE, &GPIOE_MOTOR2_init);

	GPIOE_MOTOR2_init.GPIO_Pin = GPIO_Pin_15;
	GPIO_Init(GPIOE, &GPIOE_MOTOR2_init);

	/* ���� ���� ���� �ʱ�ȭ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_14 | GPIO_Pin_15);
}

void _GPIO_MOTORInit(void) {
	_GPIO_MOTOR1Init();
	_GPIO_MOTOR2Init();
}

void _GPIO_ADCInit(void){
	/* ADC Channel 11 : GPIOC Pin 1
	 * ADC Channel 12 : GPIOC Pin 2
	 * ADC Channel 13 : GPIOC Pin 3
	 * GPIOC mode : Analog Input Mode */
	GPIO_InitTypeDef GPIOC_ADC_init;
	GPIOC_ADC_init.GPIO_Mode = GPIO_Mode_AIN;
	GPIOC_ADC_init.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_Init(GPIOC, &GPIOC_ADC_init);
}

void _GPIO_TIM3Ch3Ch1Init(void) {
	/* TIM3 Channel 3 : GPIOB Pin 0(Alternative Function)
	 * TIM3 Channel 1 : GPIOA Pin 6(Alternative Function) */
	GPIO_InitTypeDef GPIOB_TIM3Ch3Ch1_init;

	GPIOB_TIM3Ch3Ch1_init.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIOB_TIM3Ch3Ch1_init.GPIO_Speed = GPIO_Speed_50MHz;

	/* ������ ���� ���� : TIM3_Channel3(PB0) GPIO �ʱ�ȭ */
	GPIOB_TIM3Ch3Ch1_init.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIOB, &GPIOB_TIM3Ch3Ch1_init);

	/* DC���� �ӵ� ���� : TIM3_Channel1(PA6) GPIO �ʱ�ȭ */
	GPIOB_TIM3Ch3Ch1_init.GPIO_Pin = GPIO_Pin_6;
	GPIO_Init(GPIOA, &GPIOB_TIM3Ch3Ch1_init);
}

void GPIO_Configure(void) {
	_GPIO_USARTInit();
	_GPIO_LEDInit();
	_GPIO_MOTORInit();
	_GPIO_ADCInit();
	_GPIO_TIM3Ch3Ch1Init();
}

void ADC_Configure(void){
	/* ADC Channel 11(PC1), 12(PC2), 13(PC3) ��� */
	ADC_InitTypeDef ADC_InitStructure;
	ADC_DeInit(ADC1);
	ADC_InitStructure.ADC_Mode=ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode=ENABLE;
	ADC_InitStructure.ADC_ContinuousConvMode=ENABLE;
	ADC_InitStructure.ADC_ExternalTrigConv=ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign=ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel=3;
	ADC_RegularChannelConfig(ADC1,ADC_Channel_11,1,ADC_SampleTime_239Cycles5);
	ADC_RegularChannelConfig(ADC1,ADC_Channel_12,2,ADC_SampleTime_239Cycles5);
	ADC_RegularChannelConfig(ADC1,ADC_Channel_13,3,ADC_SampleTime_239Cycles5);
	ADC_Init(ADC1,&ADC_InitStructure);

	ADC_DMACmd(ADC1,ENABLE);
	ADC_Cmd(ADC1,ENABLE);
}

void ADC_Initialize(void){
	/* ADC Calibration �ʱ�ȭ */
	ADC_ResetCalibration(ADC1);
	while(ADC_GetResetCalibrationStatus(ADC1));
	ADC_StartCalibration(ADC1);
	while(ADC_GetCalibrationStatus(ADC1));

	/* ADC Convert ���� */
	ADC_SoftwareStartConvCmd(ADC1,ENABLE);
}

void _TIM3Ch3Ch1_PWM_Configure(void) {
	/* �⺻ System Clock : 72MHz
	 * Prescale = 72MHz / 1MHz - 1 = 71 => 72MHz�� 1MHz Timer clock���� ����
	 * TIM_Period = 1MHz / 50Hz = 20,000 => 1MHz Timer Clock�� 20,000�ֱ⸦ ���� 50Hz �ֱ�� ���� */
	TIM_TimeBaseInitTypeDef TIM3Ch3Ch1_Init;

	TIM_OCInitTypeDef PWM_TIM3Ch3Ch1_Init;

	TIM3Ch3Ch1_Init.TIM_Prescaler = (uint16_t)(SystemCoreClock / 1000000) - 1;
	TIM3Ch3Ch1_Init.TIM_Period = 20000 - 1;
	TIM3Ch3Ch1_Init.TIM_ClockDivision = 0;
	TIM3Ch3Ch1_Init.TIM_CounterMode = TIM_CounterMode_Down;
	TIM_TimeBaseInit(TIM3, &TIM3Ch3Ch1_Init);

	PWM_TIM3Ch3Ch1_Init.TIM_OCMode = TIM_OCMode_PWM1;
	PWM_TIM3Ch3Ch1_Init.TIM_OCPolarity = TIM_OCPolarity_High;
	PWM_TIM3Ch3Ch1_Init.TIM_OutputState = TIM_OutputState_Enable;

	/* Servo Moter 0�� : 7.5% Duty Cycle�� �ʱ� ������ PWM mode ���� */
	/* ���� ���� ���� ���� : �ʱ� duty cycle�� 7.5%�� �ʱ�ȭ */
	PWM_TIM3Ch3Ch1_Init.TIM_Pulse = 1500;
	TIM_OC3Init(TIM3, &PWM_TIM3Ch3Ch1_Init);
	TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Disable);

	/* DC ���� �ӵ� ���� : �ʱ� duty cycle�� 50%�� �ʱ�ȭ */
	PWM_TIM3Ch3Ch1_Init.TIM_Pulse = 10000;
	TIM_OC1Init(TIM3, &PWM_TIM3Ch3Ch1_Init);
	TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Disable);

	TIM_ARRPreloadConfig(TIM3, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
}

void _TIM2_Configure(void) {
	/* �⺻ System Clock : 72MHz
	 * Prescale = 72MHz / 100KHz - 1 = 719 => 72MHz�� 100KHz Timer clock���� ����
	 * TIM_Period = 100KHz / 2Hz = 50,000 => 100KHz Timer clock�� 50,000�ֱ⸦ ���� 2Hz(0.5sec) �ֱ�� ���� */
	TIM_TimeBaseInitTypeDef TIM2_Init;

	TIM2_Init.TIM_Prescaler = (uint16_t)(SystemCoreClock / 100000) - 1;
	TIM2_Init.TIM_Period = 50000 - 1;
	TIM2_Init.TIM_ClockDivision = 0;
	TIM2_Init.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM2_Init);

	/* TIM2 Interrupt Enable */
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
	TIM_Cmd(TIM2, ENABLE);
}

void _TIM1_Configure(void) {
	/* �⺻ System Clock : 72MHz
	 * Prescale = 72MHz / 100KHz - 1 = 719 => 72MHz�� 100KHz Timer clock���� ����
	 * TIM_Period = 100KHz / 1KHz - 1 = 99 => 100KHz Timer clock�� 100�ֱ⸦ ���� 1KHz(1msec) �ֱ�� ���� */
	TIM_TimeBaseInitTypeDef TIM1_Init;

	TIM1_Init.TIM_Prescaler = (uint16_t)(SystemCoreClock / 100000) - 1;
	TIM1_Init.TIM_Period = 100 - 1;
	TIM1_Init.TIM_ClockDivision = 0;
	TIM1_Init.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM1, &TIM1_Init);

	/* TIM2 Interrupt Enable */
	TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);
	TIM_Cmd(TIM1, ENABLE);
}

void TIM_PWM_Configure(void){
	_TIM3Ch3Ch1_PWM_Configure();
	_TIM2_Configure();
	_TIM1_Configure();
}

void DMA_Configure(void){
	/* DMA1 Channel 11�� Configure*/
	DMA_InitTypeDef DMA_init;

	DMA_DeInit(DMA1_Channel1);
	DMA_init.DMA_PeripheralBaseAddr=(uint32_t)&ADC1->DR;
	DMA_init.DMA_MemoryBaseAddr=(uint32_t)ADC_result_value_arr;
	DMA_init.DMA_DIR=DMA_DIR_PeripheralSRC;
	DMA_init.DMA_BufferSize=3;
	DMA_init.DMA_PeripheralInc=DMA_PeripheralInc_Disable;
	DMA_init.DMA_MemoryInc=DMA_MemoryInc_Enable;
	DMA_init.DMA_PeripheralDataSize=DMA_PeripheralDataSize_Word;
	DMA_init.DMA_MemoryDataSize=DMA_MemoryDataSize_Word;
	DMA_init.DMA_Mode=DMA_Mode_Circular;
	DMA_init.DMA_Priority=DMA_Priority_High;
	DMA_init.DMA_M2M=DMA_M2M_Disable;

	DMA_Init(DMA1_Channel1,&DMA_init);

	DMA_Cmd(DMA1_Channel1,ENABLE);
}

void _USART_ITR_Configure(void) {
	NVIC_InitTypeDef NVIC_init;

	/* USART�� ���� NVIC �⺻ �� ����(�켱���� 0) */
	NVIC_init.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_init.NVIC_IRQChannelSubPriority = 0;
	NVIC_init.NVIC_IRQChannelCmd = ENABLE;

	/* Enable the USART1 NVIC_init */
	NVIC_init.NVIC_IRQChannel = USART1_IRQn;
	NVIC_Init(&NVIC_init);

	/* Enable the USART2 NVIC_init */
	NVIC_init.NVIC_IRQChannel = USART2_IRQn;
	NVIC_Init(&NVIC_init);

	/* Enable the USART3 NVIC_init */
	NVIC_init.NVIC_IRQChannel = USART3_IRQn;
	NVIC_Init(&NVIC_init);
}

void _TIM2_ITR_Configure(void) {
	NVIC_InitTypeDef NVIC_init;
	NVIC_init.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_init.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_init.NVIC_IRQChannelSubPriority = 0;
	NVIC_init.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_init);
}

void Interrupt_Configure(void) {
	_USART_ITR_Configure();
	_TIM2_ITR_Configure();
}

/* Todo : �̰� �ʿ����� �� �𸣰ڴ�. ���߿� �ʿ������ ������ �� */
void SendString(USART_TypeDef* USARTx, char* string) {
	int i=0;
	for(i=0; i<strlen(string); i++) {
		USART_SendData(USARTx, string[i]);
		while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET);
	}
}

void _command_forward(void) {
	/* ���͵���̹�1 ���� */
	/* ���� ���� : in1(High)/in2(Low) */
	GPIO_SetBits(GPIOE, GPIO_Pin_0);
	GPIO_ResetBits(GPIOE, GPIO_Pin_1);
	/* ������ ���� : in3(High)/in4(Low) */
	GPIO_SetBits(GPIOE, GPIO_Pin_2);
	GPIO_ResetBits(GPIOE, GPIO_Pin_3);

	/* ���͵���̹�2 ���� */
	/* ���� ���� : in1(High)/in2(Low) */
	GPIO_ResetBits(GPIOE, GPIO_Pin_4);
	GPIO_SetBits(GPIOE, GPIO_Pin_5);
	/* ������ ���� : in3(High)/in4(Low) */
	GPIO_ResetBits(GPIOE, GPIO_Pin_14);
	GPIO_SetBits(GPIOE, GPIO_Pin_15);
}

void _command_backward(void) {
	/* ���͵���̹�1 ���� */
	/* ���� ���� : in1(Low)/in2(High) */
	GPIO_ResetBits(GPIOE, GPIO_Pin_0);
	GPIO_SetBits(GPIOE, GPIO_Pin_1);
	/* ������ ���� : in3(Low)/in4(High) */
	GPIO_ResetBits(GPIOE, GPIO_Pin_2);
	GPIO_SetBits(GPIOE, GPIO_Pin_3);

	/* ���͵���̹�2 ���� */
	/* ���� ���� : in1(Low)/in2(High) */
	GPIO_SetBits(GPIOE, GPIO_Pin_4);
	GPIO_ResetBits(GPIOE, GPIO_Pin_5);
	/* ������ ���� : in3(Low)/in4(High) */
	GPIO_SetBits(GPIOE, GPIO_Pin_14);
	GPIO_ResetBits(GPIOE, GPIO_Pin_15);
}

void _command_left(void) {
	/* ���͵���̹�1�� ���ʹ��� */

	/* ���͵���̹�1 ���� */
	/* ���� ���� : in1(Low)/in2(High) => ������ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_0);
	GPIO_SetBits(GPIOE, GPIO_Pin_1);
	/* ������ ���� : in3(High)/in4(Low) => ������ */
	GPIO_SetBits(GPIOE, GPIO_Pin_2);
	GPIO_ResetBits(GPIOE, GPIO_Pin_3);

	/* ���͵���̹�2 ���� */
	/* ���� ���� : in1(Low)/in2(High) => ������ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_4);
	GPIO_SetBits(GPIOE, GPIO_Pin_5);
	/* ������ ���� : in3(High)/in4(Low) => ������ */
	GPIO_SetBits(GPIOE, GPIO_Pin_14);
	GPIO_ResetBits(GPIOE, GPIO_Pin_15);
}

void _command_right(void) {
	/* ���͵���̹�1�� ���ʹ��� */

	/* ���͵���̹�1 ���� */
	/* ���� ���� : in1(High)/in2(Low) => ������ */
	GPIO_SetBits(GPIOE, GPIO_Pin_0);
	GPIO_ResetBits(GPIOE, GPIO_Pin_1);
	/* ������ ���� : in3(Low)/in4(High) => ������ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_2);
	GPIO_SetBits(GPIOE, GPIO_Pin_3);

	/* ���͵���̹�2 ���� */
	/* ���� ���� : in1(High)/in2(Low) => ������ */
	GPIO_SetBits(GPIOE, GPIO_Pin_4);
	GPIO_ResetBits(GPIOE, GPIO_Pin_5);
	/* ������ ���� : in3(Low)/in4(High) => ������ */
	GPIO_ResetBits(GPIOE, GPIO_Pin_14);
	GPIO_SetBits(GPIOE, GPIO_Pin_15);
}

void _command_stop(void) {
	/* ���͵���̹�1 ���� */
	/* ���� ���� : in1(Low)/in2(Low) => ����
	 * ������ ���� : in3(Low)/in4(Low) => ���� */
	GPIO_ResetBits(GPIOE, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3);

	/* ���͵���̹�2 ���� */
	/* ���� ���� : in1(Low)/in2(Low) => ����
	 * ������ ���� : in3(Low)/in4(Low) => ���� */
	GPIO_ResetBits(GPIOE, GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_14 | GPIO_Pin_15);
}

void command_move(int select) {
	/* Motor Driver Function
	 *    1) ������ : in1(in3) - High / in2(in4) - Low
	 *    2) ������ : in1(in3) - Low / in2(in4) - High
	 *    3) ���� : in1(in3) - High / in2(in4) - High
	 *    4) ���� : in1(in3) - Low / in2(in4) - Low */
	switch(select) {
	case 0:
		_command_stop();
		break;
	case 1:
		_command_forward();
		break;
	case 2:
		_command_backward();
		break;
	case 3:
		_command_left();
		break;
	case 4:
		_command_right();
		break;
	default:
		_command_stop();
	}
}

void change_pwm_servo_duty_cycle(int percentx10) {
	/* ## PWM ���� �ֱ� : 50Hz
	 * ## Duty Cycle
	 *    1) 3.5% : -90��
	 *    2) 7.5% : 0��
	 *    3) 11.5% : 90��
	 * ## PWM Duty Cycle ��� => TIM_Pulse = percent * 20,000 / 100 */
	TIM_OCInitTypeDef PWM_TIM3Ch3_Init;
	int pwm_pulse;
	pwm_pulse = percentx10 * 20000 / 100 / 10;
	PWM_TIM3Ch3_Init.TIM_OCMode = TIM_OCMode_PWM1;
	PWM_TIM3Ch3_Init.TIM_OCPolarity = TIM_OCPolarity_High;
	PWM_TIM3Ch3_Init.TIM_OutputState = TIM_OutputState_Enable;
	PWM_TIM3Ch3_Init.TIM_Pulse = pwm_pulse;
	TIM_OC3Init(TIM3, &PWM_TIM3Ch3_Init);
}

void speed_up_down(int percentx10) {
	/* ## �ӵ� PWM ���� �ֱ� : 15KHz
	 * PWM Duty Cycle ��� => TIM_Pulse = percent * 200 / 100 */
	TIM_OCInitTypeDef PWM_TIM3Ch1_Init;
	int pwm_pulse;
	pwm_pulse = percentx10 * 20000 / 100 / 10;
	PWM_TIM3Ch1_Init.TIM_OCMode = TIM_OCMode_PWM1;
	PWM_TIM3Ch1_Init.TIM_OCPolarity = TIM_OCPolarity_High;
	PWM_TIM3Ch1_Init.TIM_OutputState = TIM_OutputState_Enable;
	PWM_TIM3Ch1_Init.TIM_Pulse = pwm_pulse;
	TIM_OC1Init(TIM3, &PWM_TIM3Ch1_Init);
}

void AccelGyroInitialize(void) {
	set_last_read_angle_data(GetTickCount(), 0, 0, 0, 0, 0, 0);
}

void calculate_accelgyro_average(int total_readnum) {
	int i=0;
	for(i=0; i<total_readnum; i++) {
		MPU6050_GetRawAccelGyro(AccelGyro);
		base_accel_x += AccelGyro[0];
		base_accel_y += AccelGyro[1];
		base_accel_z += AccelGyro[2];
		base_gyro_x += AccelGyro[3];
		base_gyro_y += AccelGyro[4];
		base_gyro_z += AccelGyro[5];
	}
	base_accel_x /= total_readnum;
	base_accel_y /= total_readnum;
	base_accel_z /= total_readnum;
	base_gyro_x /= total_readnum;
	base_gyro_y /= total_readnum;
	base_gyro_z /= total_readnum;
}

void set_last_read_angle_data(unsigned long time, float x, float y, float z, float gyro_x, float gyro_y, float gyro_z) {
	/* ���ӵ� ���� ���� Ȱ���� ó���� ���� ���� ���� ���� ������ ���� */
	last_read_time = time;
	last_angle_x = x;
	last_angle_y = y;
	last_angle_z = z;
	last_gyro_angle_x = gyro_x;
	last_gyro_angle_y = gyro_y;
	last_gyro_angle_z = gyro_z;
}

void PrintAccelGryroRaw2Angle(int16_t accelgyro[6]) {
	/* 250 degree/s ������ ������, �� ���� 16bit ���ش����� ǥ���ϰ� �����Ƿ�,
	 * mpu-6050���� ������ ���� -32766 ~ +32766 ���̴�.
	 * �׷��Ƿ� �� ���� ���� 250 degree/s�� ��ȯ�Ϸ��� 131�� ������� �Ѵ�.
	 * ������ �ٸ��� �� ���� �ٲ��� �Ѵ�. */

	float FS_SEL = 131;

	/* ȸ���� ���� �� �ð� �˱� */
	unsigned long t_now = GetTickCount();

	float gyro_x = (accelgyro[3] - base_gyro_x)/FS_SEL;
	float gyro_y = (accelgyro[4] - base_gyro_y)/FS_SEL;
	float gyro_z = (accelgyro[5] - base_gyro_z)/FS_SEL;


	/* ���ӵ� �� ������ 16bit�̱� ������ -32766 ~ + 32766 �����̰�,
	 * +-2g �����̸� mpu-6050���κ��� �Ѿ�� ���� ���� g������ ȯ���Ϸ���
	 * scale factor(16384)�� ������ �Ѵ�. +-2g ������ +-32766���̴�. �� 32766���̸� 2�� �ȴ�. */

	//acceleration ���� ������ ����
	float accel_x = accelgyro[0];
	float accel_y = accelgyro[1];
	float accel_z = accelgyro[2];


	//accelerometer�� ���� ���� ���
	float RADIANS_TO_DEGREES = 180/3.14159;

	// float accel_vector_length = sqrt(pow(accel_x,2) + pow(accel_y,2) + pow(accel_z,2));
	float accel_angle_y = atan(-1*accel_x/sqrt(pow(accel_y,2) + pow(accel_z,2)))*RADIANS_TO_DEGREES;
	float accel_angle_x = atan(accel_y/sqrt(pow(accel_x,2) + pow(accel_z,2)))*RADIANS_TO_DEGREES;
	float accel_angle_z = 0;


	//gyro angles ���1
	float dt =(t_now - last_read_time)/1000.0;
	float gyro_angle_x = gyro_x*dt + last_angle_x;
	float gyro_angle_y = gyro_y*dt + last_angle_y;
	float gyro_angle_z = gyro_z*dt + last_angle_z;

	//gyro angles ���2
	float unfiltered_gyro_angle_x = gyro_x*dt + last_gyro_angle_x;
	float unfiltered_gyro_angle_y = gyro_y*dt + last_gyro_angle_y;
	float unfiltered_gyro_angle_z = gyro_z*dt + last_gyro_angle_z;

	//���ĸ� �̿��ؼ� ���� ���� ���3
	float alpha = 0.96;
	float angle_x = alpha*gyro_angle_x + (1.0 - alpha)*accel_angle_x;
	float angle_y = alpha*gyro_angle_y + (1.0 - alpha)*accel_angle_y;
	float angle_z = gyro_angle_z;  //Accelerometer�� z-angle ����

	//���� ���� ����
	set_last_read_angle_data(t_now, angle_x, angle_y, angle_z, unfiltered_gyro_angle_x, unfiltered_gyro_angle_y, unfiltered_gyro_angle_z);

	// 2 ����Ʈ ������ ������ ���� 100�� ���ϰ�, ���� ��, 100�� �����ش�.
	//LCD_ShowNum(20,20, (int)accel_angle_x*100,10,BLACK,WHITE);
	//LCD_ShowNum(20,40, (int)accel_angle_y*100,10,BLACK,WHITE);
	//LCD_ShowNum(20,60,(int)accel_angle_z*100,10,BLACK,WHITE);
	//LCD_ShowNum(20,80,(int)unfiltered_gyro_angle_x*100,10,BLACK,WHITE);
	//LCD_ShowNum(20,100,(int)unfiltered_gyro_angle_y*100,10,BLACK,WHITE);
	//LCD_ShowNum(20,120,(int)unfiltered_gyro_angle_z*100,10,BLACK,WHITE);
	if(angle_x > 0) {
		LCD_ShowString(200, 205, "+", BLACK, WHITE);
		LCD_ShowNum(210,205,(int)angle_x,2,BLACK,WHITE);		// ���������� �ʿ��� ��� ����
	}else {
		LCD_ShowString(200, 205, "-", BLACK, WHITE);
		LCD_ShowNum(210,205,(int)(-1)*angle_x,2,BLACK,WHITE);		// ���������� �ʿ��� ��� ����
	}
	//LCD_ShowNum(20,160,(int)angle_y,10,BLACK,WHITE);
	//LCD_ShowNum(20,180,(int)angle_z,10,BLACK,WHITE);
}

uint32_t GetTickCount(void) {
	return g_currentTick;
}

void UIOutline_Init(void) {
	LCD_Clear(WHITE);
	LCD_ShowString(40, 10, "## ProtoSmartCar ##", BLACK, WHITE);
	LCD_DrawRectangle(5, 30, 235, 170);
	LCD_ShowString(20, 40, "[+] Temperature : ", BLACK, WHITE);
	LCD_ShowString(20, 70, "[+] Humidity    : ", BLACK, WHITE);
	LCD_ShowString(20, 100, "[+] Velocity    : ", BLACK, WHITE);

	LCD_ShowString(50, 180, "- Accident Log -", BLACK, WHITE);
	LCD_DrawRectangle(5, 200, 235, 315);
}

/* Todo : ������ ����׿��̹Ƿ� ���߿� ���� �� */
//void USART1_IRQHandler(void) {
//	char recv_data;
//
//	/* USART1���� ������ �޾� ����Ѵ�.(������� ���� �뵵 & �ȵ���̵� ���÷� �۽�) */
//	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
//		recv_data = USART_ReceiveData(USART1);
//
//		if(recv_data) {
//			//USART_SendData(USART1, recv_data);								// wjdebug
//			//while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);		// wjdebug
//			USART_SendData(USART2, recv_data);
//			while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
//			USART_ClearITPendingBit(USART1, USART_IT_RXNE);
//		}
//	}
//}

void USART1_IRQHandler(void) {
	char recv_data;

	/* �ȵ���̵� ���ø����̼ǿ��� ������ ���ڿ� ����� �޾Ƽ� ��ɾ�� ó���ϴ� ���ͷ�Ʈ */
	/* Command End : "\n"
	   Command List : "FORWARD", "BACKWARD", "LEFT", "RIGHT", "STOP", "ON", "OFF"
	   Optional Command List : "LIGHT_ON", "LIGHT_OFF", "TEMPER_UP", "TEMPER_DOWN", "WIPER_ON", "WIPER_OFF"
	*/
	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
		recv_data = USART_ReceiveData(USART1);

		if(recv_data == '!') {
			if(strstr(command, "CLEAR") != NULL) {
				GPIO_ResetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
			}else if(strstr(command, "POWERON") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_2);
				//speed_up_down(600);
				//UIOutline_Init();
			}else if(strstr(command, "POWEROFF") != NULL) {
				//GPIO_ResetBits(GPIOD, GPIO_Pin_2);
				//speed_up_down(400);
				//LCD_Clear(BLACK);
			}else if(strstr(command, "FORWARD") != NULL) {
				GPIO_SetBits(GPIOD, GPIO_Pin_3);
				//speed_up_down(500);
				command_move(1);
			}else if(strstr(command, "BACKWARD") != NULL) {
				GPIO_SetBits(GPIOD, GPIO_Pin_2);
				//GPIO_SetBits(GPIOD, GPIO_Pin_4);
				command_move(2);
			}else if(strstr(command, "LEFT") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_4);
				command_move(3);
			}else if(strstr(command, "RIGHT") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_7);
				command_move(4);
			}else if(strstr(command, "STOP") != NULL) {
				GPIO_ResetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
				command_move(0);
			}
			command_pos = 0;
			memset(command, 0x00, 100);
		}else {
			command[command_pos++] = recv_data;
			USART_SendData(USART1, recv_data);								// wjdebug
			while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);		// wjdebug
		}
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}

void USART2_IRQHandler(void) {
	char recv_data;

	/* �ȵ���̵� ���ø����̼ǿ��� ������ ���ڿ� ����� �޾Ƽ� ��ɾ�� ó���ϴ� ���ͷ�Ʈ */
	/* Command End : "\n"
	   Command List : "FORWARD", "BACKWARD", "LEFT", "RIGHT", "STOP", "ON", "OFF"
	   Optional Command List : "LIGHT_ON", "LIGHT_OFF", "TEMPER_UP", "TEMPER_DOWN", "WIPER_ON", "WIPER_OFF"
	*/
	if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
		recv_data = USART_ReceiveData(USART2);

		if(recv_data == '!') {
			if(strstr(command, "CLEAR") != NULL) {
				GPIO_ResetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
			}else if(strstr(command, "POWERON") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_2);
				UIOutline_Init();
			}else if(strstr(command, "POWEROFF") != NULL) {
				//GPIO_ResetBits(GPIOD, GPIO_Pin_2);
				LCD_Clear(BLACK);
			}else if(strstr(command, "FORWARD") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_3);
				command_move(1);
			}else if(strstr(command, "BACKWARD") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_4);
				command_move(2);
			}else if(strstr(command, "LEFT") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_4);
				command_move(3);
			}else if(strstr(command, "RIGHT") != NULL) {
				//GPIO_SetBits(GPIOD, GPIO_Pin_7);
				command_move(4);
			}else if(strstr(command, "STOP") != NULL) {
				GPIO_ResetBits(GPIOD, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_7);
				command_move(0);
			}
			command_pos = 0;
			memset(command, 0x00, 100);
		}else {
			command[command_pos++] = recv_data;
			//USART_SendData(USART1, recv_data);								// wjdebug
			//while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);		// wjdebug
		}
		USART_ClearITPendingBit(USART2, USART_IT_RXNE);
	}
}

void USART3_IRQHandler(void) {
	char voice_command;

	/* USART3���� ������ �޾� ����Ѵ�.(�����νļ������� ���� �޾� PC�� ����)
	 *    1) voice_command : �����νļ����� ��ɾ �ν��ϸ� ���� ���� ��ȯ
	 *    2) ���� ���� ���� voiceBuffer�� ����� ��ɾ �ν� */
	if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
		voice_command = USART_ReceiveData(USART3);

		if(voice_command) {
			if(voice_command_enable) {
				UIOutline_Init();
				LCD_ShowString(50, 130, voiceBuffer[voice_command-1], BLACK, WHITE);
			}else {
				UIOutline_Init();
				LCD_ShowString(50, 130, "Voice Command Disabled!", BLACK, WHITE);
			}
			USART_ClearITPendingBit(USART3, USART_IT_RXNE);
		}
	}
}

void TIM2_IRQHandler(void) {
	if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
		/* ## rain_power_flag : ������ ����
		 *    1) 0 : �� �ȿ�
		 *    2) 1 : �� ����
		 *    3) 2 : �� ����
		 * ## servo_direction : �������� ���� ���� �÷��� */

		/* ���� ���Ⱑ ���� �� : 1�� �������� ���� */
		if(rain_power_flag == 1) {
			if(timer_count % 2 == 0) {
				if(servo_direction == 1) {
					change_pwm_servo_duty_cycle(35);
					servo_direction = 0;
				}
				else {
					change_pwm_servo_duty_cycle(115);
					servo_direction = 1;
				}
				timer_count = timer_count % 50;
			}
			timer_count++;

		/* ���� ���Ⱑ ���� �� : 0.5�� �������� ���� */
		}else if(rain_power_flag == 2) {
			if(servo_direction == 1) {
				change_pwm_servo_duty_cycle(35);
				servo_direction = 0;
			}
			else {
				change_pwm_servo_duty_cycle(115);
				servo_direction = 1;
			}
		}

//		if(speed_updown_timer_count % 2 == 0) {
//			if((int)last_angle_x >= 40) {
//				speed_up_down(700);
//			}else if((int)last_angle_x >= 20) {
//				speed_up_down(600);
//			}else if((int)last_angle_x <= (-1)*40) {
//				speed_up_down(300);
//			}else if((int)last_angle_x <= (-1)*20) {
//				speed_up_down(400);
//			}else {
//				speed_up_down(500);
//			}
//			speed_updown_timer_count = speed_updown_timer_count % 2;
//		}
//		speed_updown_timer_count++;

		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);		// clear interrupt flag
	}
}

void TIM1_IRQHandler(void) {
	if(TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET) {
		/* TickCount ���� 1�� ������Ű�鼭 g_currentTick�� ���� */
		g_currentTick++;
		TIM_ClearITPendingBit(TIM1, TIM_IT_Update);		// clear interrupt flag
	}
}
