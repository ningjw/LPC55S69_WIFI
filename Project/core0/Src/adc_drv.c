/* --------------------------------------------------------------------------
 *
 *
 *---------------------------------------------------------------------------*/
#include "main.h"


volatile uint32_t g_pwmPeriod   = 0U;
volatile uint32_t g_pulsePeriod = 0U;

#define DEMO_LPADC_USER_CHANNEL 13U
#define DEMO_LPADC_USER_CMDID   1U /* CMD1 */
#define DEMO_LPADC_VREF_SOURCE  kLPADC_ReferenceVoltageAlt2

TaskHandle_t BAT_TaskHandle = NULL;  /* 电池管理任务句柄 */
uint8_t status = 0;
lpadc_conv_result_t         mLpadcResult;

/*******************************************************************************
 * Code
 ******************************************************************************/
status_t CTIMER_GetPwmPeriodValue(uint32_t pwmFreqHz, uint8_t dutyCyclePercent, uint32_t timerClock_Hz)
{
    /* Calculate PWM period match value */
    g_pwmPeriod = (timerClock_Hz / pwmFreqHz) - 1;

    /* Calculate pulse width match value */
    if (dutyCyclePercent == 0)
    {
        g_pulsePeriod = g_pwmPeriod + 1;
    }
    else
    {
        g_pulsePeriod = (g_pwmPeriod * (100 - dutyCyclePercent)) / 100;
    }
    return kStatus_Success;
}



uint8_t spiTxBuff[4] = {0xFF};
uint8_t spiRxBuff[4] = {0};
spi_transfer_t xfer = {
	.txData = spiTxBuff,
	.rxData = spiRxBuff,
	.dataSize = 3,
	.configFlags = kSPI_FrameAssert,
};
/***************************************************************************************
  * @brief
  * @input
  * @return
***************************************************************************************/
uint32_t ADS1271_ReadData(void)
{
	SPI_ADCMasterTransfer(FLEXCOMM0_PERIPHERAL, &xfer);
	return ((spiRxBuff[0]<<16) | (spiRxBuff[1]<<8) | spiRxBuff[2]);
}



