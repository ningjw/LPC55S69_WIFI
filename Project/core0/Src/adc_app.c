#include "main.h"
#include "fsl_powerquad.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include "math.h"

float ShakeADC[ADC_LEN];
float Temperature[64];

TaskHandle_t ADC_TaskHandle = NULL;  /* ADC任务句柄 */

uint32_t timeCapt = 0;
char str[12];

uint32_t ADC_ShakeValue = 0;

float ADC_VoltageValue;

static float GetEnegryWindowCorrected(int windowType) {
    float NBF = 1;
    switch (windowType) {
        case 0://矩形窗0
            NBF = 1;                                                 
            break;
        case 1:   //三角窗1
            NBF = 1.33;
            break;
        case 2:   //汉宁窗2
            NBF = 1.6339;                                            
            break;
        case 3:    //哈明窗3
            NBF = 1.59;                                             
            break;
        case 4:    //布莱克曼窗4
            NBF = 1.65;//待定
            break;
        case 5:    //Kaiser-Bessel(3.0)                      凯赛窗
            NBF = 1.86;                                            
            break;
        case 6:    //top flat  平顶窗
            NBF = 2.26;                                           
            break;
        case 7:    //possion(3.0)
            NBF = 1.65;
            break;
        case 8:    //possion(4.0)
            NBF = 2.08;
            break;
        case 9:    //cauchy(4.0)
            NBF = 1.76;
            break;
        case 10:    //cauchy(5.0)
            NBF = 2.06;
            break;
        case 11:    //Gaussian(3.0)
            NBF = 1.64;
            break;
        case 12:    //Kaiser-Bessel(3.5)
            NBF = 1.93;
            break;

    }
    return NBF;
}


//矩形窗0，三角窗1，汉宁窗2，海明窗3，布莱克曼窗4，凯泽窗5。。。
static float GetRMS(float data[],int len, int windowType) 
{  
    float rms = 0.000;
    float sum = 0.000;
    float EnegryCorrected = GetEnegryWindowCorrected(windowType);
    for (int i = 0; i < len; i++) {
        sum = sum + data[i] * data[i];
    }
    float average = sum / len;
    rms = EnegryCorrected * sqrt(average);
    rms = (float) round(rms * 1000) / 1000.0f;   //保留3位小数
    return rms;
}


/***************************************************************************************
  * @brief   start adc sample
  * @input
  * @return
***************************************************************************************/
void ADC_SampleStart(uint8_t reason)
{
	DEBUG_PRINTF("%s:sampNumber=%d,SampleRate=%d,\r\n",__func__,
				g_sample_para.sampNumber,g_sample_para.SampleRate);
	
    RTC_GetDatetime(RTC, &sampTime);
	
    //系统指示灯指示正在采样,黄灯亮
    g_sys_para.sysLedStatus = SYS_IN_SAMPLING;
    GPIO_PinWrite(GPIO, BOARD_LED_SYS_RED_PORT,  BOARD_LED_SYS_RED_PIN, OFF);
    GPIO_PinWrite(GPIO, BOARD_LED_SYS_GREEN_PORT, BOARD_LED_SYS_GREEN_PIN, OFF);
    
	g_sample_para.sampleReason = reason;
	g_sys_para.tempCount = 0;
    g_sample_para.shkCount = 0;
	g_sample_para.spdCount = 0;
	memset(ShakeADC,0,ADC_LEN);
	memset(Temperature, 0, sizeof(Temperature));
	
	PWR_3V3A_ON;//开启ADC相关的电源
	PWR_5V_ON;//开启5V的滤波器电源
	//判断自动关机条件
    if(g_sys_flash_para.autoPwrOffCondition != 1) {
        g_sys_para.sysIdleCount = 0;
    }
	
	//判断点数是否超出数组界限
	if(g_sample_para.sampNumber > ADC_LEN){
		g_sample_para.sampNumber = ADC_LEN;
	}
	
	/* 输出PWM 用于LTC1063FA的时钟输入,控制采样带宽*/
	g_sample_para.Ltc1063Clk = 1000 * g_sample_para.SampleRate / 25;

	SI5351a_SetPDN(SI_CLK0_CONTROL,true);
	si5351aSetFilterClk1(g_sample_para.Ltc1063Clk);
	SI5351a_SetPDN(SI_CLK1_CONTROL,true);//配置ADC芯片时钟

	if(g_sample_para.SampleRate > 45000){
		ADC_MODE_HIGH_SPEED;//使用高速模式
		//使用PWM作为ADS1271的时钟, 其范围为37ns - 10000ns (10us)
		si5351aSetAdcClk0(g_sample_para.SampleRate * 256);
	}else{
		ADC_MODE_LOW_POWER;//使用低速模式
		//使用PWM作为ADS1271的时钟, 其范围为37ns - 10000ns (10us)
		si5351aSetAdcClk0(g_sample_para.SampleRate * 512);
	}

	vTaskDelay(1);//等待500ms
	
	//开始采集数据前获取一次温度
	Temperature[g_sys_para.tempCount++] = MXL_ReadObjTemp();
	//设置为true后,会在PIT中断中采集温度数据
	g_sys_para.WorkStatus = true;
	
	//丢弃前部分数据
	int ADC_InvalidCnt = 0;
	while (1) { //wait ads1271 ready
        while(ADC_READY == 1){};//等待ADC_READY为低电平
		ADC_ShakeValue = ADS1271_ReadData();
		ADC_InvalidCnt++;
		if(ADC_InvalidCnt > 100) break;
    }

	start_spd_caputer();

	__disable_irq();//关闭中断
	while(ADC_READY == 0){};//等待ADC_READY为高电平
	while(1) { //wait ads1271 ready
        while(ADC_READY == 1){};//等待ADC_READY为低电平
		ShakeADC[g_sample_para.shkCount++] = ADS1271_ReadData();
		if(g_sample_para.shkCount >= g_sample_para.sampNumber){
			g_sample_para.shkCount = g_sample_para.sampNumber;
			break;
		}
    }
	__enable_irq();//开启中断
	
	stop_spd_caputer();
	
	if(g_sample_para.sampNumber == 0){//Android发送中断采集命令后,该值为0
		spd_msg->len = 0;
	}
	//结束采集后获取一次温度
	Temperature[g_sys_para.tempCount++] = MXL_ReadObjTemp();
    /* 触发ADC采样完成事件  */
    xTaskNotify(ADC_TaskHandle, EVT_SAMPLE_FINISH, eSetBits);
}


/***************************************************************************************
  * @brief   stop adc sample
  * @input
  * @return
***************************************************************************************/
void ADC_SampleStop(void)
{
	DEBUG_PRINTF("ADC_SampleStop \r\n");
	/* Stop get temperature*/
	g_sys_para.WorkStatus = false;
    
    //采样结束,恢复指示灯绿色常亮.
	g_sys_para.sysLedStatus = SYS_IDLE;
    GPIO_PinWrite(GPIO, BOARD_LED_SYS_RED_PORT,  BOARD_LED_SYS_RED_PIN, OFF);
    GPIO_PinWrite(GPIO, BOARD_LED_SYS_GREEN_PORT, BOARD_LED_SYS_GREEN_PIN, ON);
    
	//关闭时钟输出
	SI5351a_SetPDN(SI_CLK0_CONTROL,false);
	SI5351a_SetPDN(SI_CLK1_CONTROL,false);

	//关闭电源
	PWR_3V3A_OFF;
	PWR_5V_OFF;//开启5V的滤波器电源

    //转速信号
    g_sample_para.spdCount = spd_msg->len;

    /* 统计平均温度,最小温度,最大温度 */
    float sum = 0;
    int min_i = 0;
    int max_i = 0;
    for(int i=0;i<g_sys_para.tempCount;i++){
        sum += Temperature[g_sys_para.tempCount];
        min_i = Temperature[i] < Temperature[min_i] ? i : min_i;
        max_i = Temperature[i] > Temperature[max_i] ? i : max_i;
    }
    g_sample_para.Process = sum / g_sys_para.tempCount;
    g_sample_para.ProcessMax = Temperature[max_i];
    g_sample_para.ProcessMin = Temperature[min_i];
    
    //计算通过WIFI发送震动信号需要多少个包
    g_sys_para.shkPacksByWifiCat1 = (g_sample_para.shkCount / ADC_NUM_WIFI_CAT1) + (g_sample_para.shkCount % ADC_NUM_WIFI_CAT1 ? 1 : 0);
    
    //计算通过WIFI发送转速信号需要多少个包
    g_sys_para.spdPacksByWifiCat1 = (g_sample_para.spdCount / ADC_NUM_WIFI_CAT1) + (g_sample_para.spdCount % ADC_NUM_WIFI_CAT1 ? 1 : 0);
    
    //计算将所有数据通过WIFI上传需要多少个包
    g_sys_para.sampPacksByWifiCat1 = g_sys_para.shkPacksByWifiCat1 + g_sys_para.spdPacksByWifiCat1 + 1;//wifi需要加上1个采样参数包
    
    //转速信号从哪个sid开始发送
    g_sys_para.spdStartSid = g_sys_para.shkPacksByWifiCat1 + 1;//需要加上1个采样参数包
}



/***********************************************************************
  * @ 函数名  ： ADC采集任务
  * @ 功能说明：
  * @ 参数    ： 无
  * @ 返回值  ： 无
  **********************************************************************/
void ADC_AppTask(void)
{
    uint32_t r_event;
    BaseType_t xReturn = pdTRUE;
#if 0
    /*以下代码打开宏后,会打开ADC电源,一直采集ADS1271数据*/
	ADC_MODE_LOW_POWER;
	PWR_5V_ON;
	PWR_3V3A_ON;
	
	si5351aSetAdcClk0(1000000);//给ADS1271提供时钟, 测试点TP6
	si5351aSetFilterClk1(1000000);//设置滤波器时钟, 测试点TP10
	
	while (1) { //wait ads1271 ready
		Temperature[0] = MXL_ReadObjTemp();
        while(ADC_READY == 1){};//等待ADC_READY为低电平
		ADC_ShakeValue = ADS1271_ReadData();
		ADC_VoltageValue = ADC_ShakeValue * 2.048 / 0x800000;
    }

	SI5351a_SetPDN(SI_CLK0_CONTROL, false);
	SI5351a_SetPDN(SI_CLK1_CONTROL, false);

	PWR_3V3A_OFF;//关闭ADC采集相关的电源
	PWR_5V_OFF;
#endif
	
    DEBUG_PRINTF("ADC_AppTask Running\r\n");
	
//    xTaskNotify(ADC_TaskHandle, EVT_SAMPLE_START, eSetBits);
    
    while(1)
    {
        /*等待ADC完成采样事件*/
        xReturn = xTaskNotifyWait(pdFALSE, ULONG_MAX, &r_event, portMAX_DELAY);
		
		/* 开始采样事件*/
		if(r_event & EVT_SAMPLE_START)
		{
			ADC_SampleStart(AUTO_SAMPLE);
		}
		/* 触发休眠*/
		else if(r_event & EVT_ENTER_SLEEP)
		{
			SystemSleep();
		}
		/* 完成采样事件*/
		else if(r_event & EVT_SAMPLE_FINISH) 
		{
            ADC_SampleStop();
			W25Q128_AddAdcData();
			/* ---------------将震动信号转换-----------------------*/
#if 0
			float tempValue = 0;
			for(uint32_t i = 0; i < g_sample_para.shkCount; i++) {
				if((uint32_t)ShakeADC[i] < 0x800000){
					ShakeADC[i] = ShakeADC[i] * g_sample_para.bias * 1.0f / 0x800000;
				}else{
					ShakeADC[i] = ((ShakeADC[i] - 0x800000) * g_sample_para.bias * 1.0f / 0x800000) - g_sample_para.bias;
				}
				DEBUG_PRINTF("%01.5f,",ShakeADC[i]);
			}
			
			g_sys_para.shkRMS = GetRMS(ShakeADC, g_sample_para.shkCount, g_sample_para.WindowsType);
#endif          
			/*通知线程采样完成, 可以获取采样数据了*/
			xTaskNotifyGive( BLE_WIFI_TaskHandle);
		}
	}
}



