#include <stdint.h>

#define CONSOLE_SIZE 2048
volatile char console_buf[CONSOLE_SIZE];
volatile uint32_t console_wp = 0;
volatile uint32_t console_rp = 0;
volatile int test_capture_flag = 0;          /* 0=idle,1=start-req,2=capturing,3=stop-req,4=ready */
volatile uint32_t test_capture_start_wp = 0;
volatile char test_out[512];                 /* small linear buffer to view test output */
volatile uint32_t test_out_len = 0;
int manual_mode = 0;       /* 0 = simulator, 1 = use manual inputs */
float hr_input = 75.0f;
float temp_input = 36.8f;
float bp_input = 110.0f;
static void console_putc(char c) {
    uint32_t wp = console_wp;
    console_buf[wp] = c;
    wp = (wp + 1) % CONSOLE_SIZE;
    if (wp == console_rp) {
        console_rp = (console_rp + 1) % CONSOLE_SIZE; /* overwrite oldest */
    }
    console_wp = wp;
}

static void console_write(const char *s) {
    while (s && *s) console_putc(*s++);
}

/* uint -> string (returns pointer to start in buffer) */
static char *uitoa_buf(uint32_t v, char *buf_end) {
    char *p = buf_end;
    if (v == 0) { *--p = '0'; return p; }
    while (v) {
        *--p = (char)('0' + (v % 10));
        v /= 10;
    }
    return p;
}

/* float -> string with two decimals */
static void float_to_str2(float x, char *out) {
    if (x < 0.0f) { *out++ = '-'; x = -x; }
    uint32_t ip = (uint32_t)x;
    uint32_t fp = (uint32_t)((x - (float)ip) * 100.0f + 0.5f);
    if (fp >= 100) { ip += 1; fp -= 100; }
    char tmp[16];
    char *s = uitoa_buf(ip, tmp + sizeof(tmp));
    while (*s) *out++ = *s++;
    *out++ = '.';
    *out++ = (char)('0' + (fp / 10) % 10);
    *out++ = (char)('0' + (fp % 10));
    *out = '\0';
}

/* --- Sensor simulator (deterministic) --- */
static float sim_t;
static void sensor_sim_init(void) { sim_t = 0.0f; }
int test_mode = 0;
int test_iters = 0;

static void sensor_sim_step(float *hr, float *temp, float *bp) {
   
	 if (manual_mode) {
        *hr   = hr_input;
        *temp = temp_input;
        *bp   = bp_input;
        return;
    }
	 if (test_iters > 0) {
        if (test_mode == 1) {
            /* WARNING-level (mildly out of normal range) */
            *hr   = 55.0f;   /* below HR_LOW -> warning */
            *temp = 36.5f;
            *bp   = 115.0f;
        } else if (test_mode == 2) {
            /* EMERGENCY-level (critical) */
            *hr   = 180.0f;  /* above HR_CRIT_HIGH -> emergency */
            *temp = 42.0f;
            *bp   = 200.0f;
        }
        test_iters--;
        return; /* skip normal sim for this step */
    }
 
	int phase = ((int)sim_t) % 1000;
    /* simple wave-like variations without math lib */
    float hr_base  = 80.0f  + 8.0f  * (((phase % 40) < 20) ? 1.0f : -1.0f) * ((phase % 20) / 20.0f);
    float temp_base= 36.8f  + 0.3f  * (((phase % 200) < 100) ? 1.0f : -1.0f) * ((phase % 100) / 100.0f);
    float bp_base  = 110.0f + 6.0f  * (((phase % 80) < 40) ? 1.0f : -1.0f) * ((phase % 40) / 40.0f);

    if ((int)sim_t % 40 == 5)  hr_base  += 40.0f;
    if ((int)sim_t % 40 == 15) temp_base+= 3.0f;
    if ((int)sim_t % 40 == 25) bp_base  += 40.0f;

    float noise = (float)(((int)(sim_t * 7) % 7) - 3) * 0.2f;

    *hr   = hr_base  + noise;
    *temp = temp_base+ noise * 0.02f;
    *bp   = bp_base  + noise * 0.5f;

    sim_t += 1.0f;
}

/* --- Monitoring algorithm --- */
typedef enum { STATE_NORMAL=0, STATE_WARNING=1, STATE_EMERGENCY=2 } monitor_state_t;

#define WIN_SZ 8
static float hr_win[WIN_SZ], tmp_win[WIN_SZ], bp_win[WIN_SZ];
static int win_idx, win_full;
static monitor_state_t cur_state;

static const float HR_LOW = 60.0f, HR_HIGH = 100.0f;
static const float HR_CLOW = 40.0f, HR_CHIGH = 140.0f;
static const float T_LOW = 36.1f, T_HIGH = 37.2f;
static const float T_CLOW = 34.0f, T_CHIGH = 40.0f;
static const float BP_LOW = 90.0f, BP_HIGH = 120.0f;
static const float BP_CLOW = 60.0f, BP_CHIGH = 180.0f;

static int warn_cnt, emerg_cnt;
static const int WARN_REQ = 3, EMERG_REQ = 2;

static void push_win(float *a, float v) { a[win_idx] = v; }
static float mean_win(const float *a) {
    int n = win_full ? WIN_SZ : win_idx;
    if (n <= 0) return 0.0f;
    float s=0.0f;
    for (int i=0;i<n;i++) s += a[i];
    return s / (float)n;
}

/* --- Display helpers into console_buf --- */
static void append_label_value(const char *label, float v, const char *unit) {
    char tmp[80], num[32]; char *p = tmp;
    const char *q = label;
    while (*q) *p++ = *q++;
    *p++ = ':'; *p++ = ' ';
    float_to_str2(v, num);
    q = num; while (*q) *p++ = *q++;
    *p++ = ' '; q = unit; while (*q) *p++ = *q++;
    *p++ = '\n'; *p = '\0';
    console_write(tmp);
}

static void display_summary(float hr, float temp, float bp) {
    console_write("Patient Monitoring Simulation\n");
    console_write("-----------------------------\n");
    append_label_value("Heart Rate", hr, "bpm");
    append_label_value("Temperature", temp, "C");
    append_label_value("Blood Pressure", bp, "mmHg");
    console_write("-----------------------------\n");
}

static void display_alarm(const char *m) {
    console_write("ALARM: "); console_write(m); console_write("\n");
}

/* --- Monitor logic --- */
static void monitor_init(void) {
    for (int i=0;i<WIN_SZ;i++) hr_win[i]=tmp_win[i]=bp_win[i]=0.0f;
    win_idx = 0; win_full = 0; cur_state = STATE_NORMAL; warn_cnt = emerg_cnt = 0;
}

static void monitor_step(float hr, float temp, float bp) {
    push_win(hr_win, hr); push_win(tmp_win, temp); push_win(bp_win, bp);
    win_idx++; if (win_idx >= WIN_SZ) { win_idx = 0; win_full = 1; }

    float hr_avg = mean_win(hr_win);
    float t_avg  = mean_win(tmp_win);
    float bp_avg = mean_win(bp_win);

    int any_warn = 0, any_emerg = 0;
    if (hr_avg < HR_CLOW || hr_avg > HR_CHIGH) any_emerg = 1;
    else if (hr_avg < HR_LOW || hr_avg > HR_HIGH) any_warn = 1;

    if (t_avg < T_CLOW || t_avg > T_CHIGH) any_emerg = 1;
    else if (t_avg < T_LOW || t_avg > T_HIGH) any_warn = 1;

    if (bp_avg < BP_CLOW || bp_avg > BP_CHIGH) any_emerg = 1;
    else if (bp_avg < BP_LOW || bp_avg > BP_HIGH) any_warn = 1;

    if (any_emerg) { emerg_cnt++; warn_cnt = 0; }
    else if (any_warn) { warn_cnt++; emerg_cnt = 0; }
    else { warn_cnt = emerg_cnt = 0; }

    monitor_state_t new_state;
if (emerg_cnt >= EMERG_REQ) {
    new_state = STATE_EMERGENCY;
} else if (warn_cnt >= WARN_REQ) {
    new_state = STATE_WARNING;
} else {
    new_state = STATE_NORMAL;
}

if (new_state != cur_state) {
    cur_state = new_state;
    if (cur_state == STATE_EMERGENCY) {
        display_alarm("EMERGENCY: critical vitals");
    } else if (cur_state == STATE_WARNING) {
        display_alarm("WARNING: vitals outside normal");
    } else {
        console_write("STATE: NORMAL\n");
    }
}
		
    /* continuous output */
    display_summary(hr_avg, t_avg, bp_avg);
		if (test_capture_flag == 1) {
        /* start requested: record current write pointer as start */
        test_capture_start_wp = console_wp;
			console_write("CAPTURE_START_STATE: ");
        if (cur_state == STATE_NORMAL) {
            console_write("NORMAL\n");
        } else if (cur_state == STATE_WARNING) {
            console_write("WARNING\n");
        } else {
            console_write("EMERGENCY\n");
        }
        test_capture_flag = 2; /* now capturing */
    } else if (test_capture_flag == 3) {
        /* stop requested: copy from start to current wp into linear buffer */
        uint32_t src = test_capture_start_wp;
        uint32_t dst = 0;
        uint32_t end = console_wp;
        while (src != end && dst < (sizeof(test_out) - 1)) {
            test_out[dst++] = console_buf[src];
            src = (src + 1) % CONSOLE_SIZE;
        }
        test_out[dst] = '\0';
        test_out_len = dst;
        test_capture_flag = 4; /* ready to view */
			}
}

/* --- main --- */
int main(void) {
    for (uint32_t i=0;i<CONSOLE_SIZE;i++) console_buf[i]=0;
    console_wp = console_rp = 0;

    sensor_sim_init();
    monitor_init();

    float hr=0.0f, temp=0.0f, bp=0.0f;
    for (int i=0;i<200;i++) {
        sensor_sim_step(&hr, &temp, &bp);
        monitor_step(hr, temp, bp);
        for (volatile int d=0; d<200000; ++d) { __asm("nop"); }
    }

    while (1) { __asm("nop"); }
    return 0;
}
