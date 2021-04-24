#ifndef __BLE_APP_H
#define __BLE_APP_H



extern uint16_t g_wifiRxCnt;
extern TaskHandle_t BLE_WIFI_TaskHandle ;  /* À¶ÑÀÈÎÎñ¾ä±ú */
extern uint8_t g_commTxBuf[];
extern uint8_t g_wifiRxBuf[];
extern uint8_t g_wifiStartRx;
extern uint32_t  g_wifiRxTimeCnt;

void BLE_WIFI_AppTask(void);
void WIFI_SendStr(const char *str);


#endif
