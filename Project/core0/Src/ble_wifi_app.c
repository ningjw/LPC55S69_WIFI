#include "main.h"

#define DEVICE_BLE_NAME "BLE Communication"

extern void LPUART2_init(void);


uint8_t g_wifiRxBuf[FLEXCOMM_BUFF_LEN] = {0};//���ڽ��ջ�����

uint16_t g_wifiRxCnt = 0;
uint8_t g_wifiStartRx = false;
uint32_t  g_wifiRxTimeCnt = 0;
uint32_t ble_event = 0;
uint32_t BleStartFlag = false;

TaskHandle_t        BLE_WIFI_TaskHandle = NULL;//����������


/***************************************************************************************
  * @brief   ����һ���ַ���
  * @input   base:ѡ��˿�; data:��Ҫ���͵�����
  * @return
***************************************************************************************/
void WIFI_SendStr(const char *str)
{
	USART_WriteBlocking(FLEXCOMM2_PERIPHERAL, (uint8_t *)str, strlen(str));
}

/*****************************************************************
* ���ܣ�����ATָ��
* ����: send_buf:���͵��ַ���
		recv_str���ڴ������а��������ַ���
        p_at_cfg��AT����
* �����ִ�н������
******************************************************************/
uint8_t WIFI_SendCmd(const char *cmd, const char *recv_str, uint16_t time_out)
{
    uint8_t try_cnt = 0;
	g_wifiRxCnt = 0;
	memset(g_wifiRxBuf, 0, FLEXCOMM_BUFF_LEN);
retry:
    WIFI_SendStr(cmd);//����ATָ��
    /*wait resp_time*/
    xTaskNotifyWait(pdFALSE, ULONG_MAX, &ble_event, time_out);
    //���յ��������а�����Ӧ������
    if(strstr((char *)g_wifiRxBuf, recv_str) != NULL) {
		DEBUG_PRINTF("%s\r\n",g_wifiRxBuf);
        return true;
    } else {
        if(try_cnt++ > 3) {
			DEBUG_PRINTF("send AT cmd fail\r\n");
            return false;
        }
		DEBUG_PRINTF("retry: %s\r\n",cmd);
        goto retry;//����
    }
}

/***************************************************************************************
  * @brief   ����WIFIģ��ΪAp����ģʽ
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
  * @ ������  �� BLE_WIFI_AppTask
  * @ ����˵����
  * @ ����    �� ��
  * @ ����ֵ  �� ��
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
		
            /* ��������/wifi���� */
            sendBuf = ParseProtocol(g_wifiRxBuf);
			
			/* �Ƿ��������������ݰ� */
			if( g_sys_flash_para.firmCore0Update == BOOT_NEW_VERSION) {
				//����������Nor Flash
				Flash_SavePara();
				//�ر������ж�,����λϵͳ
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
        //��ս��ܵ�������
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
		else if(g_wifiRxTimeCnt >= 10) { //10msδ���ܵ�����,��ʾ�������ݳ�ʱ
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

    /*���ڽ��յ�����*/
    if ( USART_GetStatusFlags(FLEXCOMM2_PERIPHERAL) & (kUSART_RxFifoNotEmptyFlag | kUSART_RxError) )
    {
        /*��ȡ����*/
        ucTemp = USART_ReadByte(FLEXCOMM2_PERIPHERAL);
		
		g_wifiStartRx = true;
		g_wifiRxTimeCnt = 0;
		g_sys_para.sysIdleCount = 0;/* ���ܵ��������ݾ���0������*/
		if(g_wifiRxCnt < FLEXCOMM_BUFF_LEN) {
			/* �����ܵ������ݱ��浽����*/
			g_wifiRxBuf[g_wifiRxCnt++] = ucTemp;
		}
		
		if(g_sys_para.BleWifiLedStatus != BLE_WIFI_UPDATE && g_wifiRxBuf[g_wifiRxCnt-1] == '}'){
			/* �������,�ñ�־��0*/
			g_wifiStartRx = false;
			xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_OK, eSetBits);
		}else if (g_sys_para.BleWifiLedStatus==BLE_WIFI_UPDATE && g_wifiRxCnt >= FIRM_ONE_PACKE_LEN){
			/* �������,�ñ�־��0*/
			g_wifiStartRx = false;
			xTaskNotify(BLE_WIFI_TaskHandle, EVT_UART_OK, eSetBits);
		}
    }
    __DSB();
}

