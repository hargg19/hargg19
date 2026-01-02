/*!
    \file    main.c
    \brief   TIMER16 dma demo

    \version 2025-01-01, V2.5.0, firmware for GD32F3x0
*/

/*
    Copyright (c) 2025, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32f3x0.h"
#include <stdio.h>


#define TIMER0_CH0CC  ((uint32_t)0x40012c34)
uint16_t buffer[3] = {249, 499, 749};

void gpio_config(void);
void timer_config(void);
void dma_config(void);

/*!
    \brief      configure the GPIO ports
    \param[in]  none
    \param[out] none
    \retval     none
*/
void gpio_config(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);

    /*configure PB8(TIMER16 CH0) as alternate function*/
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_9);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    gpio_af_set(GPIOB, GPIO_AF_2, GPIO_PIN_9);
}

/*!
    \brief      configure the DMA peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
void dma_config(void)
{
    dma_parameter_struct dma_init_struct;

    /* enable DMA clock */
    rcu_periph_clock_enable(RCU_DMA);
    syscfg_dma_remap_enable(SYSCFG_DMA_REMAP_TIMER16);
    /* initialize DMA channel4 */
    dma_deinit(DMA_CH1);

    /* DMA channel4 initialize */
    dma_deinit(DMA_CH1);
    dma_init_struct.direction    = DMA_MEMORY_TO_PERIPHERAL;
    dma_init_struct.memory_addr  = (uint32_t)buffer;
    dma_init_struct.memory_inc   = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma_init_struct.number       = 3;
    dma_init_struct.periph_addr  = (uint32_t)TIMER0_CH0CC;
    dma_init_struct.periph_inc   = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma_init_struct.priority     = DMA_PRIORITY_HIGH;
    dma_init(DMA_CH1, &dma_init_struct);

    /* configure DMA mode */
    dma_circulation_enable(DMA_CH1);
    dma_memory_to_memory_disable(DMA_CH1);

    /* enable DMA channel4 */
    dma_channel_enable(DMA_CH1);
}

/*!
    \brief      configure the TIMER peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
void timer_config(void)
{
    /* TIMER16 DMA Transfer example -------------------------------------------------
    TIMER0CLK is fixed to systemcoreclock, the TIMER16 prescaler is equal to 108(GD32F350)
    so the TIMER16 counter clock used is 1MHz.

    the objective is to configure TIMER16 channel 1 to generate PWM
    signal with a frequency equal to 1KHz and a variable duty cycle(25%,50%,75%) that is
    changed by the DMA after a specific number of update DMA request.

    the number of this repetitive requests is defined by the TIMER16 repetition counter,
    each 2 update requests, the TIMER16 Channel 0 duty cycle changes to the next new
    value defined by the buffer .
    -----------------------------------------------------------------------------*/
    timer_oc_parameter_struct timer_ocintpara;
    timer_parameter_struct timer_initpara;

    rcu_periph_clock_enable(RCU_TIMER16);

    timer_deinit(TIMER16);

    /* TIMER16 configuration */
    timer_initpara.prescaler         = 45;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = 135;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 1;
    timer_init(TIMER16, &timer_initpara);

    /* CH0 configuration in PWM0 mode */
    timer_ocintpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocintpara.outputnstate = TIMER_CCXN_ENABLE;
    timer_ocintpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocintpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocintpara.ocidlestate  = TIMER_OC_IDLE_STATE_HIGH;
    timer_ocintpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;
    timer_channel_output_config(TIMER16, TIMER_CH_0, &timer_ocintpara);

    timer_channel_output_pulse_value_config(TIMER16, TIMER_CH_0, buffer[0]);
    timer_channel_output_mode_config(TIMER16, TIMER_CH_0, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(TIMER16, TIMER_CH_0, TIMER_OC_SHADOW_DISABLE);

    /* TIMER16 primary output enable */
    timer_primary_output_config(TIMER16, ENABLE);

    /* TIMER16 update DMA request enable */
    timer_dma_enable(TIMER16, TIMER_DMA_UPD);

    /* auto-reload preload enable */
    timer_auto_reload_shadow_enable(TIMER16);

    /* TIMER16 counter enable */
    timer_enable(TIMER16);
}

/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    gpio_config();
    dma_config();
    timer_config();

    while(1);
}