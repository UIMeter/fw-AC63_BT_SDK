#include "typedef.h"
#include "asm/clock.h"
#include "asm/adc_api.h"
#include "timer.h"
#include "init.h"
#include "asm/efuse.h"
#include "irq.h"
#include "asm/power/p33.h"
#include "asm/power_interface.h"
#include "jiffies.h"

u32 adc_sample(u32 ch);

static volatile u16 _adc_res;
static volatile u16 cur_ch_value;
static u8 cur_ch = 0;

struct adc_info_t {
    u32 ch;
    u16 value;
    u32 jiffies;
    u32 sample_period;
};

#define     ENABLE_OCCUPY_MODE 1

static struct adc_info_t adc_queue[ADC_MAX_CH + ENABLE_OCCUPY_MODE];

static u16 vbg_adc_value;

#define     ADC_SRC_CLK clk_get("adc")

/*config adc clk according to sys_clk*/
static const u32 sys2adc_clk_info[] = {
    128000000L,
    96000000L,
    72000000L,
    48000000L,
    24000000L,
    12000000L,
    6000000L,
    1000000L,
};

u32 adc_add_sample_ch(u32 ch)
{
    u32 i = 0;
    for (i = 0; i < ADC_MAX_CH; i++) {
        /* printf("%s() %d %x %x\n", __func__, i, ch, adc_queue[i].ch); */
        if (adc_queue[i].ch == ch) {
            break;
        } else if (adc_queue[i].ch == -1) {
            adc_queue[i].ch = ch;
            adc_queue[i].value = 1;
            adc_queue[i].jiffies = 0;
            adc_queue[i].sample_period = msecs_to_jiffies(0);
            printf("add sample ch %x\n", ch);
            break;
        }
    }
    return i;
}

u32 adc_set_sample_freq(u32 ch, u32 ms)
{
    u32 i;
    for (i = 0; i < ADC_MAX_CH; i++) {
        if (adc_queue[i].ch == ch) {
            adc_queue[i].sample_period = msecs_to_jiffies(ms);
            adc_queue[i].jiffies = msecs_to_jiffies(ms) + jiffies;
            break;
        }
    }
    return i;
}

u32 adc_remove_sample_ch(u32 ch)
{
    u32 i = 0;
    for (i = 0; i < ADC_MAX_CH; i++) {
        if (adc_queue[i].ch == ch) {
            adc_queue[i].ch = -1;
            break;
        }
    }
    return i;
}
static u32 adc_get_next_ch(u32 cur_ch)
{
    for (int i = cur_ch + 1; i < ADC_MAX_CH; i++) {
        if (adc_queue[i].ch != -1) {
            return i;
        }
    }
    return 0;
}
u32 adc_get_value(u32 ch)
{
    if (ch == AD_CH_LDOREF) {
        return vbg_adc_value;    
    } 

    for (int i = 0; i < ADC_MAX_CH; i++) {
        if (adc_queue[i].ch == ch) {
            return adc_queue[i].value;
        }
    }
    return 0;
}

static u32 adc_value_to_voltage(u32 adc_vbg, u32 adc_ch_val)
{
#define CENTER  801 

    u32 adc_res = adc_ch_val;
    u32 adc_trim = get_vbg_trim();
    /* if ((adc_trim &0x1f) == 0x1f) { */
    /*     adc_trim = 0; */
    /* } */
    u32 tmp, tmp1;
    tmp1 = adc_trim & 0x1f;
    tmp = (adc_trim & BIT(5)) ? CENTER - tmp1 * 3 : CENTER + tmp1 * 3;
    adc_res = adc_res * tmp / adc_vbg;
    return adc_res;
}

u32 adc_get_voltage(u32 ch)
{
#ifdef CONFIG_FPGA_ENABLE
    return 1000;
#endif
    u32 adc_vbg = adc_get_value(AD_CH_LDOREF);
    u32 adc_res = adc_get_value(ch);
    return adc_value_to_voltage(adc_vbg, adc_res);
}

u32 adc_check_vbat_lowpower()
{
    u32 vbat = adc_get_value(AD_CH_VBAT);
    return __builtin_abs(vbat-255) < 5;
}

void adc_close()
{
    JL_ADC->CON = 0;
    JL_ADC->CON = 0;
}
void adc_suspend()
{
    JL_ADC->CON &= ~BIT(4);
}
void adc_resume()
{
    JL_ADC->CON |= BIT(4);
}

void adc_enter_occupy_mode(u32 ch)
{
    if (JL_ADC->CON & BIT(4)) {
        return;
    }
    adc_queue[ADC_MAX_CH].ch = ch;
    cur_ch_value = adc_sample(ch);
}
void adc_exit_occupy_mode()
{
    adc_queue[ADC_MAX_CH].ch = -1;
}
u32 adc_occupy_run()
{
    if (adc_queue[ADC_MAX_CH].ch != -1) {
        while (1) {
            asm volatile("nop");
            if (_adc_res != (u16) - 1) {
                break;
            }
        }
        if (_adc_res == 0) {
            _adc_res ++;
        }
        adc_queue[ADC_MAX_CH].value = _adc_res;
        _adc_res = cur_ch_value;
        return adc_queue[ADC_MAX_CH].value;
    }
    return 0;
}
u32 adc_get_occupy_value()
{
    if (adc_queue[ADC_MAX_CH].ch != -1) {
        return adc_queue[ADC_MAX_CH].value;
    }
    return 0;
}
u32 get_adc_div(u32 src_clk)
{
    u32 adc_clk;
    u32 adc_clk_idx;
    u32 cnt;
    adc_clk = src_clk;
    cnt = ARRAY_SIZE(sys2adc_clk_info);
    for (adc_clk_idx = 0; adc_clk_idx < cnt; adc_clk_idx ++) {
        if (adc_clk > sys2adc_clk_info[adc_clk_idx]) {
            break;
        }
    }

    if (adc_clk_idx < cnt) {
        adc_clk_idx = cnt - adc_clk_idx;
    } else {
        adc_clk_idx = cnt - 1;
    }
    return adc_clk_idx;
}

___interrupt
static void adc_isr()
{
    _adc_res = JL_ADC->RES;

    adc_pmu_detect_en(AD_CH_WVDD >> 20);
    local_irq_disable();
    JL_ADC->CON = BIT(6);
    JL_ADC->CON = 0;

    local_irq_enable();
}

u32 adc_sample(u32 ch)
{
    const u32 tmp_adc_res = _adc_res;
    _adc_res = (u16) - 1;

    u32 adc_con = 0;
    SFR(adc_con, 0, 3, 0b110);//div 96

    adc_con |= (0xf << 12); //启动延时控制，实际启动延时为此数值*8个ADC时钟
    adc_con |= BIT(3);
    adc_con |= BIT(6);
    adc_con |= BIT(5);//ie

    SFR(adc_con, 8, 4, ch & 0xf);

    if ((ch & 0xffff) == AD_CH_PMU) {
        adc_pmu_ch_select(ch >> 16);
    } else if ((ch & 0xffff) == AD_CH_BT) {
    } else if ((ch & 0xffff) == AD_CH_SYS_PLL) {
    }

    JL_ADC->CON = adc_con;
    JL_ADC->CON |= BIT(4);//en

    JL_ADC->CON |= BIT(6);//kistart

    return tmp_adc_res;
}

static u8 change_vbg_flag = 0;
void set_change_vbg_value_flag(void)
{
    change_vbg_flag = 1;
}

void adc_scan(void *priv)
{
    static u16 adc_sample_flag = 0;

    if (adc_queue[ADC_MAX_CH].ch != -1) {//occupy mode
        return;
    }

    if (JL_ADC->CON & BIT(4)) {
        return ;
    }
    
    if (adc_sample_flag) {
        if (adc_sample_flag == 2) {
            vbg_adc_value = _adc_res;
        } else {
            adc_queue[cur_ch].value = _adc_res;
        }
        adc_sample_flag = 0;
    }

    if (change_vbg_flag) {
        change_vbg_flag = 0;
        adc_sample(AD_CH_LDOREF);
        adc_sample_flag = 2;
        return;
    }

    u8 next_ch = adc_get_next_ch(cur_ch);

    if (adc_queue[next_ch].sample_period) {
        if (time_before(adc_queue[next_ch].jiffies, jiffies)) {
            adc_sample(adc_queue[next_ch].ch);
            adc_sample_flag = 1;
            adc_queue[next_ch].jiffies = adc_queue[next_ch].sample_period + jiffies;
        }
    } else {
        adc_sample(adc_queue[next_ch].ch);
        adc_sample_flag = 1;
    }

    cur_ch = next_ch;
}

void _adc_init(u32 sys_lvd_en)
{
    memset(adc_queue, 0xff, sizeof(adc_queue));

    JL_ADC->CON = 0;
    JL_ADC->CON = 0;

    u32 vbat_queue_ch = adc_add_sample_ch(AD_CH_VBAT);
    adc_set_sample_freq(AD_CH_VBAT, 30000);

    adc_pmu_detect_en(1);

    u32 i;
    vbg_adc_value = 0;
    adc_sample(AD_CH_LDOREF);
    for (i = 0; i < 10; i++) {
        while(!(JL_ADC->CON & BIT(7)));
        vbg_adc_value += JL_ADC->RES;
        JL_ADC->CON |= BIT(6);
    }
    vbg_adc_value /= 10;
    printf("vbg_adc_value = %d\n", vbg_adc_value);

    adc_sample(AD_CH_VBAT);
    while(!(JL_ADC->CON & BIT(7)));
    _adc_res = JL_ADC->RES;
    adc_queue[vbat_queue_ch].value = _adc_res;

    request_irq(IRQ_SARADC_IDX, 0, adc_isr, 0);

    sys_s_hi_timer_add(NULL, adc_scan, 2); //2ms

    /* void adc_test();                              */
    /* sys_s_hi_timer_add(NULL, adc_test, 1000); //2ms */

    /* extern void wdt_close(); */
    /* wdt_close(); */
    /*  */
    /* while(1); */
}


typedef enum {
    closest,        //最接近目标值，不管偏大还是偏小
    closest_min,    //最接近目标值，并小于目标值
    closest_max,    //最接近目标值，并大于目标值
} choose_type;
static u8 get_aims_level(u16 aims, u16 *tmp_buf, u8 len, choose_type choose)
{
    u16 diff;
    u16 min_diff0 = -1;
    u16 min_diff1 = -1;
    u8 closest_min_level = -1;
    u8 closest_max_level = -1;
    for (u8 i = 0; i < len; i ++) {
        if (tmp_buf[i] >= aims) {
            diff = tmp_buf[i] - aims;
            if (diff < min_diff0) {
                min_diff0 = diff;
                closest_max_level = i;
            }
        } else {
            diff = aims - tmp_buf[i];
            if (diff < min_diff1) {
                min_diff1 = diff;
                closest_min_level = i;
            }
        }
    }
    u8 closest_level = -1;
    if (min_diff0 < min_diff1) {
        closest_level = closest_max_level;
    } else {
        closest_level = closest_min_level;
    }
    u8 res_level = -1;
    if (choose == closest_min) {
        res_level = closest_min_level;
    } else if (choose == closest_max) {
        res_level = closest_max_level;
    } else {
        res_level = closest_level;
    }
    return res_level;
}

static u32 get_ch_voltage(u32 ch)
{
    adc_pmu_detect_en(1);
    u32 i;
    u32 tmp_vbg = 0;
    adc_sample(AD_CH_LDOREF);
    for (i = 0; i < 10; i++) {
        while (!(JL_ADC->CON & BIT(7)));
        tmp_vbg += JL_ADC->RES;
        JL_ADC->CON |= BIT(6);
    }
    tmp_vbg /= 10;

    u32 tmp_ch_val = 0;
    adc_sample(ch);
    for (int i = 0; i < 10; i++) {
        while (!(JL_ADC->CON & BIT(7)));
        tmp_ch_val += JL_ADC->RES;
        JL_ADC->CON |= BIT(6);
    }
    tmp_ch_val /= 10;
    
    return adc_value_to_voltage(tmp_vbg, tmp_ch_val);
}

static u16 sysvdd_voltage[16];
static void record_sysvdd_voltage(void)
{
    u8 old_level = P33_CON_GET(P3_ANA_CON9);
    for (u8 i = 0; i < 16; i ++) { 
        P33_CON_SET(P3_ANA_CON9, 0, 4, i);
        delay(2000);
        sysvdd_voltage[i] = get_ch_voltage(AD_CH_SYSVDD);
    }
    P33_CON_SET(P3_ANA_CON9, 0, 4, old_level);
}

#define sysvdd_default_level    0b1101
u8 get_sysvdd_aims_level(u16 aims_mv)
{
    if (sysvdd_voltage[0] == 0) {
        return sysvdd_default_level;
    }
    return get_aims_level(aims_mv, sysvdd_voltage, 16, closest_max);
}

static u16 vdc13_voltage[8];
static void record_vdc13_voltage(void)
{
    u8 old_level = P33_CON_GET(P3_ANA_CON6);
    for (u8 i = 0; i < 8; i ++) { 
        P33_CON_SET(P3_ANA_CON6, 0, 3, i);
        delay(2000);
        vdc13_voltage[i] = get_ch_voltage(AD_CH_VDC13);
    }
    P33_CON_SET(P3_ANA_CON6, 0, 3, old_level);
}

#define vdc13_default_level    0b110
u8 get_vdc13_aims_level(u16 aims_mv)
{
    if (vdc13_voltage[0] == 0) {
        return vdc13_default_level;
    }
    return get_aims_level(aims_mv, vdc13_voltage, 8, closest_max);
}

static u16 mvddio_voltage[8] = {0};
AT(.volatile_ram_code)
static void record_mvddio_voltage(void)
{
    u32 vbg_center = adc_value_to_voltage(1, 1);
    u8 old_vddio_level = P33_CON_GET(P3_ANA_CON5);
    u8 old_vlvd_level = P33_CON_GET(P3_VLVD_CON);
    P33_CON_SET(P3_VLVD_CON, 6, 1, 1);  //clr_pend
    P33_CON_SET(P3_VLVD_CON, 0, 1, 0);  //关掉LVD
    adc_pmu_detect_en(1);
    adc_sample(AD_CH_LDOREF);
    u32 tmp_cnt;
    for (u8 i = 0; i < 8; i ++) { 
        P33_CON_SET(P3_ANA_CON5, 0, 3, i);
        tmp_cnt = 2000;
        while(tmp_cnt --) {
            asm ("nop");
        }
        JL_ADC->CON |= BIT(6);
        while(!(JL_ADC->CON & BIT(7)));
        tmp_cnt = JL_ADC->RES;
        mvddio_voltage[i] = vbg_center * 1024 / tmp_cnt;
    }
    P33_CON_SET(P3_ANA_CON5, 0, 3, old_vddio_level);
    tmp_cnt = 2000;
    while(tmp_cnt --) {
        asm ("nop");
    }
    P33_CON_SET(P3_VLVD_CON, 0, 1, old_vlvd_level & 0x1);
}

#define mvddio_default_level    0b110
u8 get_mvddio_aims_level(u16 aims_mv)
{
    if (mvddio_voltage[0] == 0) {
        return mvddio_default_level;
    }
    return get_aims_level(aims_mv, mvddio_voltage, 8, closest_max);
}


void adc_init()
{
    record_sysvdd_voltage();
    record_vdc13_voltage();
    record_mvddio_voltage();

    _adc_init(1);
}

void adc_dump(void)
{
    for (int i = 0; i < ARRAY_SIZE(sysvdd_voltage); i++) {
        log_i("sysvdd_voltage[%d] %d", i, sysvdd_voltage[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(vdc13_voltage); i++) {
        log_i("vdc13_voltage[%d] %d", i, vdc13_voltage[i]);
    }

    for (int i = 0; i < ARRAY_SIZE(mvddio_voltage); i++) {
        log_i("mvddio_voltage[%d] %d", i, mvddio_voltage[i]);
    }
}

void adc_test()
{
    /* printf("\n\n%s() chip_id :%x\n", __func__, get_chip_id()); */
    /* printf("%s() vbg trim:%x\n", __func__, get_vbg_trim());    */
    /* printf("%s() vbat trim:%x\n", __func__, get_vbat_trim());  */

    /* printf("\n\nWLA_CON0 %x\n", JL_ANA->WLA_CON0); */
    /* printf("WLA_CON9 %x\n", JL_ANA->WLA_CON9); */
    /* printf("WLA_CON10 %x\n", JL_ANA->WLA_CON10); */
    /* printf("WLA_CON21 %x\n", JL_ANA->WLA_CON21); */
    /*  */
    /* printf("ADA_CON %x\n", JL_ANA->ADA_CON3); */
    /* printf("PLL_CON1 %x\n", JL_CLOCK->PLL_CON1); */

    printf("\n%s() VBAT:%d %d mv\n\n", __func__,
           adc_get_value(AD_CH_VBAT), adc_get_voltage(AD_CH_VBAT) * 4);
}
void adc_vbg_init()
{
    return ;
}
//__initcall(adc_vbg_init);