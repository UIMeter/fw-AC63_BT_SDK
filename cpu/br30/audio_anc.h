#ifndef AUDIO_ANC_H
#define AUDIO_ANC_H

#include "generic/typedef.h"
#include "anc.h"
#include "app_config.h"
#include "in_ear_detect/in_ear_manage.h"


#define ANC_COEFF_SAVE_ENABLE	1	/*ANC滤波器表保存使能*/
#define ANC_INFO_SAVE_ENABLE	0	/*ANC信息记忆:保存上一次关机时所处的降噪模式等等*/
#define ANC_TONE_PREEMPTION		0	/*ANC提示音打断播放(1)还是叠加播放(0)*/
#define ANC_TRANSPARENCY_ONLY	0	/*仅支持通透模式*/
#define ANC_BOX_READ_COEFF		1	/*支持通过工具读取ANC训练系数*/
#define ANC_FADE_EN				1	/*ANC淡入淡出使能*/
#define ANC_MODE_SYSVDD_EN 		0	/*ANC模式提高SYSVDD，避免某些IC电压太低导致ADC模块工作不正常*/

#define ANC_TRAIN_MODE			ANC_FF_EN
#define ANC_MIC_TYPE			(A_MIC0|A_MIC1)	/*ANC 硬件MIC类型：模拟MIC0|模拟MIC1*/
#define ANC_MIC_CH_SWAP_EN      0   /*交换MIC0/MIC1的数据 使能之后 MIC0 = 误差MIC; MIC1 = 参考MIC*/


/***************************************ANC训练参数配置*********************************************/
#define ANC_SZ_LOW_THR			10000		/*在未收敛情况下，MIC的下限阈值，用于判断MIC是否能工作*/
#define ANC_FZ_LOW_THR			50          /*在未收敛情况下，MIC的下限阈值，用于判断MIC是否能工作*/
#define	ANC_NON_ADAPTIVE_TIME   2000		/*静音训练关闭自适应时间(单位:ms)*/
#define	ANC_SZ_ADAPTIVE_TIME    3000		/*静音训练SZ打开自适应时间*/
#define	ANC_FZ_ADAPTIVE_TIME    3000		/*静音训练FZ打开自适应时间*/
#define	ANC_WZ_TRAIN_TIME	    15000		/*噪声训练WZ训练时间*/
#define	ANC_TRAIN_STEP    		10			/*ANC训练系数*/
/***************************************************************************************************/

/*ANC模式调试信息*/
static const char *anc_mode_str[] = {
    "NULL",			/*无效/非法*/
    "ANC_OFF",		/*关闭模式*/
    "ANC_ON",		/*降噪模式*/
    "Transparency",	/*通透模式*/
    "ANC_TRAIN",	/*训练模式*/
};

/*ANC状态调试信息*/
static const char *anc_state_str[] = {
    "anc_close",	/*关闭状态*/
    "anc_init",		/*初始化状态*/
    "anc_open",		/*打开状态*/
};

/*ANC状态*/
enum {
    ANC_STA_CLOSE = 0,
    ANC_STA_INIT,
    ANC_STA_OPEN,
};

/*ANC MSG List*/
enum {
    ANC_MSG_TRAIN_OPEN = 0xA1,
    ANC_MSG_TRAIN_CLOSE,
    ANC_MSG_RUN,
    ANC_MSG_FADE_END,
    ANC_MSG_MODE_SYNC,
    ANC_MSG_TONE_SYNC,
};

#ifdef CONFIG_ANC_30C_ENABLE
//混合馈训练相关配置
#if(ANC_TRAIN_MODE & ANC_HYBRID_EN)
#define ANC_FILT_ORDER		   			2	/*range(1-3)*/
#define ANC_SR							4
#if(ANC_FILT_ORDER * 2 / ( ANC_SR - 2 ) > ANC_FILT_ORDER_MAX)
#error "FILT overflow"
#endif//(ANC_FILT_ORDER * 2 / ( ANC_SR - 2 ) > ANC_FILT_ORDER_MAX)

//前馈训练相关配置
#else
#define ANC_FILT_ORDER		   			2	/*range(1-3)*/
#define ANC_SR							3
#if(ANC_FILT_ORDER / ( ANC_SR - 2 ) > ANC_FILT_ORDER_MAX)
#error "FILT overflow"
#endif//(ANC_FILT_ORDER / ( ANC_SR - 2 ) > ANC_FILT_ORDER_MAX)
#endif//(ANC_TRAIN_MODE & ANC_HYBRID_EN)

#define ANC_TRANS_FILT_ORDER			1	/*range(1-3)*/

#endif//CONFIG_ANC_30C_ENABLE

#define ANC_TRAIN_WAY 			ANC_AUTO_BT_SPP//ANC_MANA_UART
#if ((ANC_TRAIN_WAY == ANC_AUTO_BT_SPP) && TCFG_AUDIO_ANC_ENABLE)
#if (APP_ONLINE_DEBUG == 0) //在线APP调试
#error "Need to open APP_ONLINE_DEBUG"
#endif
#endif


/*ANC记忆信息*/
typedef struct {
    u8 mode;		/*当前模式*/
    u8 mode_enable; /*使能的模式*/
#if INEAR_ANC_UI
    u8 inear_tws_mode;
#endif
    //s32 coeff[488];
} anc_info_t;

/*ANC初始化*/
void anc_init(void);

/*ANC训练模式*/
void anc_train_open(void);

/*ANC关闭训练*/
void anc_train_close(void);

/*
 *ANC状态获取
 *return 0: idle(初始化)
 *return 1: busy(ANC/通透/训练)
 */
u8 anc_status_get(void);

u8 anc_mode_get(void);

#define ANC_DAC_CH_L	0
#define ANC_DAC_CH_R	1
/*获取anc模式，dac左右声道的增益*/
u8 anc_dac_gain_get(u8 ch);

/*获取anc模式，ref_mic的增益*/
u8 anc_mic_gain_get(void);

/*ANC模式切换(切换到指定模式)，并配置是否播放提示音*/
void anc_mode_switch(u8 mode, u8 tone_play);

/*ANC模式同步(tws模式)*/
void anc_mode_sync(u8 mode);

void anc_poweron(void);

/*ANC poweroff*/
void anc_poweroff(void);

/*ANC模式切换(下一个模式)*/
void anc_mode_next(void);

/*ANC通过ui菜单选择anc模式,处理快速切换的情景*/
void anc_ui_mode_sel(u8 mode, u8 tone_play);

/*设置ANC支持切换的模式*/
void anc_mode_enable_set(u8 mode_enable);

/*
 *获取anc训练状态
 *0:未训练
 *1:训练过
 */
u8 anc_train_status_get(void);

/*
 *查询当前ANC是否处于训练状态
 *@return 1:处于训练状态
 *@return 0:其他状态
 */
int anc_train_open_query(void);

/*tws同步播放模式提示音，并且同步进入anc模式*/
void anc_tone_sync_play(int tone_name);

/*anc coeff读接口*/
int *anc_coeff_read(void);

/*anc coeff写接口*/
int anc_coeff_write(int *coeff, u16 len);

/*ANC挂起*/
void anc_suspend(void);

/*ANC恢复*/
void anc_resume(void);

/*设置结果回调函数*/
void anc_api_set_callback(void (*callback)(u8, u8), void (*pow_callback)(u8, u8));

/*设置淡入淡出使能*/
void anc_api_set_fade_en(u8 en);

/*获取参数结构体*/
anc_train_para_t *anc_api_get_train_param(void);

/*获取步进*/
u8 anc_api_get_train_step(void);

#define ANC_CFG_READ	0x01
#define ANC_CFG_WRITE	0x02
int anc_cfg_online_deal(u8 cmd, anc_gain_t *cfg);
void anc_param_fill(anc_gain_t *cfg);

extern int anc_uart_write(u8 *buf, u8 len);
extern void ci_send_packet(u32 id, u8 *packet, int size);
extern void sys_auto_shut_down_enable(void);
extern void sys_auto_shut_down_disable(void);


#endif/*AUDIO_ANC_H*/
