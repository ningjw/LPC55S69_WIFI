#include "main.h"

#define DEVICE_BLE_NAME "BLE Communication"

extern void LPUART2_init(void);


uint8_t g_wifiRxBuf[FLEXCOMM_BUFF_LEN] = {0};//串口接收缓冲区

uint16_t g_wifiRxCnt = 0;
uint8_t g_wifiStartRx = false;
uint32_t  g_wifiRxTimeCnt = 0;
uint32_t ble_event = 0;
uint32_t BleStartFlag = false;

TaskHandle_t        BLE_WIFI_TaskHandle = NULL;//蓝牙任务句柄


/***************************************************************************************
  * @brief   发送一个字符串
  * @input   base:选择端口; data:将要发送的数据
  * @return
***************************************************************************************/
void WIFI_SendStr(const char *str)
{
	USART_WriteBlocking(FLEXCOMM2_PERIPHERAL, (uint8_t *)str, strlen(str));
}

/*****************************************************************
* 功能：发送AT指令
* 输入: send_buf:发送的字符串
		recv_str：期待回令中包含的子字符串
        p_at_cfg：AT配置
* 输出：执行结果代码
******************************************************************/
uint8_t WIFI_SendCmd(const char *cmd, const char *recv_str, uint16_t time_out)
{
    uint8_t try_cnt = 0;
	g_wifiRxCnt = 0;
	memset(g_wifiRxBuf, 0, FLEXCOMM_BUFF_LEN);
retry:
    WIFI_SendStr(cmd);//发送AT指令
    /*wait resp_time*/
    xTaskNotifyWait(pdFALSE, ULONG_MAX, &ble_event, time_out);
    //接收到的数据中包含响应的数据
    if(strstr((char *)g_wifiRxBuf, recv_str) != NULL) {
		DEBUG_PRINTF("%s\r\n",g_wifiRxBuf);
        return true;
    } else {
        if(try_cnt++ > 3) {
			DEBUG_PRINTF("send AT cmd fail\r\n");
            return false;
        }
		DEBUG_PRINTF("retry: %s\r\n",cmd);
        goto retry;//重试
    }
}

/***************************************************************************************
  * @brief   设置WIFI模块为Ap工作模式
  * @input   
  * @return
***************************************************************************************/
void WIFI_Init(void)
{
	vTaskDelay(2000);
//	if(g_sys_flash_para.WifiBleInitFlag != 0xAA)
	{
		while(WIFI_SendCmd("+++", "a", 1000) == false){
			vTaskDelay(10);
		}
		
		if(WIFI_SendCmd("a","OK", 1000)==false)
		{
			DEBUG_PRINTF("********** WIFI Init error \r\n");
			g_sys_para.sysLedStatus = SYS_ERROR;
			return;
		}
//		WIFI_SendCmd("AT+RELD\r\n","OK", 300);
		
		WIFI_SendCmd("AT+E=off\r\n","OK", 300);
		
//		WIFI_SendCmd("AT+UART=115200,8,1,NONE,NFC\r\n", "OK", 300);
		
//		WIFI_SendCmd("AT+WMODE=AP\r\n","OK", 300);
		
		WIFI_SendCmd("AT+WAP=USR-C322-,None\r\n","OK", 300);
		
//		WIFI_SendCmd("AT+CHANNEL=1\r\n", "OK", 300);

//		WIFI_SendCmd("AT+LANN=192.168.1.1,255.255.255.0\r\n", "OK", 300);
		
//		WIFI_SendCmd("AT+SOCKA=TCPS,192.168.1.1,8899\r\n", "OK", 300);
	
		WIFI_SendCmd("AT+ENTM\r\n", "OK", 300);

		g_sys_flash_para.WifiBleInitFlag = 0xAA;
		
		Flash_SavePara();
	}
	g_sys_para.BleWifiLedStatus = BLE_WIFI_READY;
	DEBUG_PRINTF("USR-C322 Init OK\r\n");
}


/***********************************************************************
  * @ 函数名  ： BLE_WIFI_AppTask
  * @ 功能说明：
  * @ 参数    ： 无
  * @ 返回值  ： 无
  **********************************************************************/
void BLE_WIFI_AppTask(void)
{
    uint8_t xReturn = pdFALSE;
    DEBUG_PRINTF("BLE_WIFI_AppTask Running\r\n");
    uint8_t* sendBuf = NULL;

	WIFI_Init();
    
	BleStartFlag = true;
    memset(g_wifiRxBuf, 0, FLEXCOMM_BUFF_LEN);
    g_wifiRxCnt = 0;
	g_wifiRxTimeCnt = 0;
	g_wifiStartRx = false;
    while(1)
    {
        /*wait task notify*/
        xReturn = xTaskNotifyWait(pdFALSE, ULONG_MAX, &ble_event, portMAX_DELAY);
        if ( xReturn && ble_event == EVT_UART_OK) {
		
            /* 处理蓝牙/wifi数据 */
            sendBuf = ParseProtocol(g_wifiRxBuf);
			
			/* 是否接受完成整个数据包 */
			if( g_sys_flash_para.firmCore0Update == BOOT_NEW_VERSION) {
				//将参数存入Nor Flash
				Flash_SavePara();
				//关闭所有中断,并复位系统
				NVIC_SystemReset();
			}
			
			if( NULL != sendBuf )
            {
                WIFI_SendStr((char *)sendBuf);
                DEBUG_PRINTF("reply wifi data:\r\n%s\r\n",sendBuf);
                free(sendBuf);
                sendBuf = NULL;
            }
        }
        //清空接受到的数据
        memset(g_wifiRxBuf, 0, FLEXCOMM_BUFF_LEN);
        g_wifiRxCnt = 0;
    }
}


/***************************************************************************************
  * @brief
  * @input
  * @return
***************************************************************************************/
void FLEXCOMM2_TimeTick(void)
{
    if(g_wifiStartRx && BleStartFlag)
    {
        g_wifiRxTimeCnt++;
		if(g_sys_para.BleWifiLedStatus == BLE_WIFI_UPDATE){
			if(g_wifiRxTimeCnt >= 1000 ){
				g_wifiRxTimeCnt = 0;
				g_wifiStartRx = false;
				DEBUG_PRINTF("\nReceive time out\n", g_wifiRxCnt);
				for(uint8_t i = 0;i<g_wifiRxCnt; i++){
					DEBUG_PRINTF("%02x ",g_wifiRxBuf[i]);
				}
				xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_OK, eSetBits);
			}
		}
		else if(g_wifiRxTimeCnt >= 10) { //10ms未接受到数据,表示接受数据超时
			g_wifiRxTimeCnt = 0;
			g_wifiStartRx = false;
			xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_TIMTOUT, eSetBits);
        }
    }
}


/***************************************************************************************
  * @brief
  * @input
  * @return
***************************************************************************************/
void FLEXCOMM2_IRQHandler(void)
{
    uint8_t ucTemp;

    /*串口接收到数据*/
    if ( USART_GetStatusFlags(FLEXCOMM2_PERIPHERAL) & (kUSART_RxFifoNotEmptyFlag | kUSART_RxError) )
    {
        /*读取数据*/
        ucTemp = USART_ReadByte(FLEXCOMM2_PERIPHERAL);
		
		g_wifiStartRx = true;
		g_wifiRxTimeCnt = 0;
		g_sys_para.sysIdleCount = 0;/* 接受到蓝牙数据就清0计数器*/
		if(g_wifiRxCnt < FLEXCOMM_BUFF_LEN) {
			/* 将接受到的数据保存到数组*/
			g_wifiRxBuf[g_wifiRxCnt++] = ucTemp;
		}
		
		if(g_sys_para.BleWifiLedStatus != BLE_WIFI_UPDATE && g_wifiRxBuf[g_wifiRxCnt-1] == '}'){
			/* 接受完成,该标志清0*/
			g_wifiStartRx = false;
			xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_OK, eSetBits);
		}else if (g_sys_para.BleWifiLedStatus==BLE_WIFI_UPDATE && g_wifiRxCnt >= FIRM_ONE_PACKE_LEN){
			/* 接受完成,该标志清0*/
			g_wifiStartRx = false;
			xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_OK, eSetBits);
		}
    }
    __DSB();
}

