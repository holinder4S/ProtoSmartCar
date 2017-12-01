#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "SHT15.h"
#include <math.h>

/*************************************************************
  Function   : SHT15_Dly
  Description: SHT15 Delay
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_Dly(void)
{
    uint16_t i;
    for(i = 500; i > 0; i--);
}


/*************************************************************
  Function   : SHT15_Config
  Description: SHT15 �� �ʱ�ȭ
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;        
    /* SHT15 �� Ŭ�� �ʱ�ȭ */
    RCC_APB2PeriphClockCmd(SHT15_RCC_Periph ,ENABLE);
            
    /* DATA push-pull Output */
    GPIO_InitStructure.GPIO_Pin = SHT15_DATA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(SHT15_DATA_PORT, &GPIO_InitStructure);
    /* SCK push-pull Output */
    GPIO_InitStructure.GPIO_Pin = SHT15_SCK_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;    
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(SHT15_SCK_PORT, &GPIO_InitStructure);

    /* ��� �缳�� */
    SHT15_ConReset();
}


/*************************************************************
  Function   : SHT15_DATAOut
  Description: Data Pin�� ��¸��� ����
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_DATAOut(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    /* Data Pin push-pull Output���� ���� */
    GPIO_InitStructure.GPIO_Pin = SHT15_DATA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;         
    GPIO_Init(SHT15_DATA_PORT, &GPIO_InitStructure);
}


/*************************************************************
  Function   : SHT15_DATAIn
  Description: Data Pin�� �Է¸��� ����
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_DATAIn(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    /* Data �÷��� Input���� ���� */
    GPIO_InitStructure.GPIO_Pin = SHT15_DATA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(SHT15_DATA_PORT, &GPIO_InitStructure);
}


/*************************************************************
  Function   : SHT15_WriteByte
  Description: 1byte ����
  Input      : value:�� ����Ʈ
  return     : err: 0-����  1-����
*************************************************************/
uint8_t SHT15_WriteByte(uint8_t value)
{
    uint8_t i, err = 0;
    
    SHT15_DATAOut();                // Data������ ������� ����

    for(i = 0x80; i > 0; i /= 2)    // 1byte ����
    {
        if(i & value)
                SHT15_DATA_H();
        else
                SHT15_DATA_L();
        SHT15_Dly();
        SHT15_SCK_H();
        SHT15_Dly();
        SHT15_SCK_L();
        SHT15_Dly();
    }
    SHT15_DATAIn();                 // Data������ �Է����� �����ϰ� Data������ ����
    SHT15_SCK_H();
    err = SHT15_DATA_R();           // SHT15 Ȯ�� ��Ʈ �б�
    SHT15_SCK_L();

    return err;
}

/*************************************************************
  Function   : SHT15_ReadByte
  Description: 1byte ������ �б�
  Input      : Ack: 0-no_answer  1-answer
  return     : err: 0-���� 1-����
*************************************************************/
uint8_t SHT15_ReadByte(uint8_t Ack)
{
    uint8_t i, val = 0;

    SHT15_DATAIn();                // Data ������ �Է����� ����
    for(i = 0x80; i > 0; i /= 2)   // 1byte ������ �б�
    {
        SHT15_Dly();
        SHT15_SCK_H();
        SHT15_Dly();
        if(SHT15_DATA_R())
        {
           val = (val | i);
        }
        SHT15_SCK_L();
    }
    SHT15_DATAOut();               // Data ������ ������� ����
    if(Ack)
            SHT15_DATA_L();        // ACK�� ������, �� ���� �����͸� �б�
    else
            SHT15_DATA_H();        // ACK�� ���� ���ϸ�, ������ ��
    SHT15_Dly();
    SHT15_SCK_H();
    SHT15_Dly();
    SHT15_SCK_L();
    SHT15_Dly();

    return val;                    // Read�� �� ��ȯ
}


/*************************************************************
  Function   : SHT15_TransStart
  Description: Transmit�� �����ϸ� ������ ���� Ÿ�̹��� ����
                     _____         ________
               DATA:      |_______|
                         ___     ___
               SCK : ___|   |___|   |______        
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_TransStart(void)
{
    SHT15_DATAOut();               // Data ������ ������� ����

    SHT15_DATA_H();
    SHT15_SCK_L();
    SHT15_Dly();
    SHT15_SCK_H();
    SHT15_Dly();
    SHT15_DATA_L();
    SHT15_Dly();
    SHT15_SCK_L();
    SHT15_Dly();
    SHT15_SCK_H();
    SHT15_Dly();
    SHT15_DATA_H();
    SHT15_Dly();
    SHT15_SCK_L();
}


/*************************************************************
  Function   : SHT15_ConReset
  Description: ��� �缳���ϴ� �Լ�, �Ʒ��� ���� Ÿ�̹� Ŭ���� ����
                     _____________________________________________________         ________
               DATA:                                                      |_______|
                        _    _    _    _    _    _    _    _    _        ___     ___
               SCK : __| |__| |__| |__| |__| |__| |__| |__| |__| |______|   |___|   |______
  Input      : none        
  return     : none    
*************************************************************/
void SHT15_ConReset(void)
{
    uint8_t i;

    SHT15_DATAOut();

    SHT15_DATA_H();
    SHT15_SCK_L();

    for(i = 0; i < 9; i++)              // SCK Ŭ���� 9�� Trigger
    {
        SHT15_SCK_H();
        SHT15_Dly();
        SHT15_SCK_L();
        SHT15_Dly();
    }
    SHT15_TransStart();                 // ���� ����
}



/*************************************************************
  Function   : SHT15_SoftReset
  Description: ����Ʈ ����
  Input      : none        
  return     : err: 0-����  1-����
*************************************************************/
uint8_t SHT15_SoftReset(void)
{
    uint8_t err = 0;

    SHT15_ConReset();                   // ��� �缳��
    err += SHT15_WriteByte(SOFTRESET);  // RESET ��� ����

    return err;
}


/*************************************************************
  Function   : SHT15_ReadStatusReg
  Description: Status �������� �б�
  Input      : p_value-������ �б�, p_checksun-üũ�� ������ �б�
  return     : err: 0-����  1-����
*************************************************************/
uint8_t SHT15_ReadStatusReg(uint8_t *p_value, uint8_t *p_checksum)
{
    uint8_t err = 0;

    SHT15_TransStart();						// ���� ����
    err = SHT15_WriteByte(STATUS_REG_R);	// Status �������� ����� �б����� STATUS_REG_R�� ��.
    *p_value = SHT15_ReadByte(ACK);         // Status ������ �б�
    *p_checksum = SHT15_ReadByte(noACK);	// checksum ������ �б�
    
    return err;
}



/*************************************************************
  Function   : SHT15_WriteStatusReg
  Description: Status �������� ����
  Input      : p_value-����� ������ ��
  return     : err: 0-����  1-����
*************************************************************/
uint8_t SHT15_WriteStatusReg(uint8_t *p_value)
{
        uint8_t err = 0;

        SHT15_TransStart();                     // ���� ����
        err += SHT15_WriteByte(STATUS_REG_W);   // ���� �������Ϳ� ���� ���� STATUS_REG_W�� ��
        err += SHT15_WriteByte(*p_value);       // ���� �� �ۼ�

        return err;
}



/*************************************************************
  Function   : SHT15_Measure
  Description: �µ� �� ���� ���� �б�
  Input      : p_value-���� ��, p_checksum-üũ�� ��
  return     : err: 0-���� 1������
*************************************************************/
uint8_t SHT15_Measure(uint16_t *p_value, uint8_t *p_checksum, uint8_t mode)
{
    uint8_t err = 0;
    uint32_t i;
    uint8_t value_H = 0;
    uint8_t value_L = 0;

    SHT15_TransStart();								// ���� ����
    switch(mode)                                                         
    {
    case TEMP:										// �µ� ����
        err += SHT15_WriteByte(MEASURE_TEMP);		// �µ��� �����ϱ� ���� MEASURE_TEMP�� ��
        break;
    case HUMI:
        err += SHT15_WriteByte(MEASURE_HUMI);		// ������ �����ϱ� ���� MEASURE_HUMI�� ��
        break;
    default:
        break;
    }
    if(err != 0)
    {
        return err;
    }
    SHT15_DATAIn();
    for(i = 0; i < 1200000; i++)					// Data ��ȣ�� Low�� �ɶ����� ��ٸ�
    {
        if(SHT15_DATA_R() == 0) break;				// DATA�� ���� ������ ���� ������ �� ����
    }
    if(SHT15_DATA_R() == 1)							// ��� �ð��� �ʰ� �� ���
    {
        err += 1;
        return err;
    }
    value_H = SHT15_ReadByte(ACK);
    value_L = SHT15_ReadByte(ACK);
    *p_checksum = SHT15_ReadByte(noACK);			// Calibration ������ �б�
    *p_value = (value_H << 8) | value_L;
    return err;
}


/*************************************************************
  Function   : SHT15_Calculate
  Description: �µ� �� ���� �� ���
  Input      : Temp-�������� ���� �µ� ��, Humi-�������� ���� ���� ��
               p_humidity-���� ���� ���� ��, p_temperature-���� ���� �µ� ��
  return     : none    
*************************************************************/
void SHT15_Calculate(uint16_t t, uint16_t rh, float *p_temperature, float *p_humidity)
{
    // Datasheet�� �Ű� ���� ������
    const float d1 = -39.55;
    const float d2 = +0.01;
    const float C1 = -4;
    const float C2 = +0.0405;
    const float C3 = -0.0000028;        
    const float T1 = +0.01;
    const float T2 = +0.00008;

    float RH_Lin;                                                     // RH ���� ��
    float RH_Ture;                                                    // RH ���� ��
    float temp_C;

    temp_C = d1 + d2 * t;                                            // �µ� �� ���
    RH_Lin = C1 + C2 * rh + C3 * rh * rh;                            // ���� �� ���
    RH_Ture = (temp_C -25) * (T1 + T2 * rh) + RH_Lin;                // ���� �µ� ����, ���� ���� �� ���
    RH_Ture = (RH_Ture > 100) ? 100 : RH_Ture;
    RH_Ture = (RH_Ture < 0.1) ? 0.1 : RH_Ture;                       // ���� ���Ѽ� ����

    *p_humidity = RH_Ture;
    *p_temperature = temp_C;

}


/*************************************************************
  Function   : SHT15_CalcuDewPoint
  Description: �̽��� ���
  Input      : h-���� ����, t-���� �µ�
  return     : dew_point-�̽���
*************************************************************/
float SHT15_CalcuDewPoint(float t, float h)
{
    float logEx, dew_point;

    logEx = 0.66077 + 7.5 * t / (237.3 + t) + (log10(h) - 2);
    dew_point = ((0.66077 - logEx) * 237.3) / (logEx - 8.16077);

    return dew_point; 
}


//int main(void)
//{
//        uint16_t humi_val, temp_val;
//        uint8_t err = 0, checksum = 0;
//        float humi_val_real = 0.0;
//        float temp_val_real = 0.0;
//        float dew_point = 0.0;
//
//        BSP_Init();
//        printf("\nSHT15 Sensor Test!!!\n");
//        SHT15_Config();
//        while(1)
//        {
//                err += SHT15_Measure(&temp_val, &checksum, TEMP);                  // �µ� ����
//                err += SHT15_Measure(&humi_val, &checksum, HUMI);                  // ���� ����
//                if(err != 0)
//                        SHT15_ConReset();
//                else
//                {
//                        SHT15_Calculate(temp_val, humi_val, &temp_val_real, &humi_val_real);	// ���� �µ� �� ���� �� ���
//                        dew_point = SHT15_CalcuDewPoint(temp_val_real, humi_val_real);		// �̽��� �µ� ���
//                }
//                printf("Ambient temperature : %2.1f�棬Humidity : %2.1f%%��dew_point_temperature : %2.1f��\r\n", temp_val_real, humi_val_real, dew_point);
//                LED1_Toggle();
//                Delay_ms(1000);
//        }
//}





