#include "wifi.h"
#include "../drivers/rtlwifi.h"
#include "../kernel/sta.h"
#include "../kernel/net.h"

__attribute__((weak)) int  rtlwifi_probe(void) { return 0; }
__attribute__((weak)) void rtlwifi_rxstat(unsigned *v) { for (int i = 0; i < 6; i++) v[i] = 0; }
__attribute__((weak)) int  rtlwifi_present(void) { return 0; }
__attribute__((weak)) int  rtlwifi_start(void) { return -1; }
__attribute__((weak)) int  rtlwifi_started(void) { return 0; }
__attribute__((weak)) int  rtlwifi_have_blob(void) { return 0; }
__attribute__((weak)) int  rtlwifi_mac_get(u8 *m) { (void)m; return -1; }
__attribute__((weak)) const char *rtlwifi_name(void) { return ""; }
__attribute__((weak)) void rtlwifi_tx80211(const u8 *f, u32 n) { (void)f; (void)n; }
__attribute__((weak)) void rtlwifi_set_channel(int c) { (void)c; }
__attribute__((weak)) void rtlwifi_set_bssid(const u8 *b) { (void)b; }
__attribute__((weak)) void rtlwifi_set_link(int u) { (void)u; }
__attribute__((weak)) void rtlwifi_poll(void (*rx)(const u8 *, u32, int)) { (void)rx; }
__attribute__((weak)) void net_init(const u8 *m, net_tx_fn t) { (void)m; (void)t; }
__attribute__((weak)) void net_info(net_info_t *i) { i->up = 0; i->ip = 0; }
__attribute__((weak)) void net_rx(const u8 *f, u32 n) { (void)f; (void)n; }
__attribute__((weak)) void net_ip_fmt(u32 ip, char *b) { (void)ip; b[0] = 0; }

static int st = WIFI_NONE, probed, pending, frames, hw_ready;
static const char *msg = "";
static u8 mac[6];
static char info[96];
static int ord[STA_MAX_NETS], nord, sel_cur = -1, connected_once;
static char sv_ssid[33], sv_pass[64], try_ssid[33], try_pass[64];   /* сохранённая сеть и сеть, к которой подключаемся */
static int pref_on, boot_auto, auto_tried;
#define KNOWN_EXTRA 3                                              /* ещё три запомненные сети кроме последней */
static char kn_ssid[KNOWN_EXTRA][33], kn_pass[KNOWN_EXTRA][64];
static unsigned retry_at; static int retries;                      /* повторные попытки входа в запомненную сеть */
#define RETRY_TICKS 1500                                           /* 15 с между попытками */
#define RETRY_MAX 12
__attribute__((weak)) void prefs_flush(void) { }

static void scp(char *d, const char *s, int cap) { int i = 0; while (s && s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int seq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
void wifi_pref_get(char *ssid, char *pass, int *on) { scp(ssid, sv_ssid, 33); scp(pass, sv_pass, 64); *on = pref_on; }
void wifi_pref_set(const char *ssid, const char *pass, int on) { scp(sv_ssid, ssid, 33); scp(sv_pass, pass, 64); pref_on = on; boot_auto = on; }
void wifi_known_get(int i, char *ssid, char *pass) { if (i < 0 || i >= KNOWN_EXTRA) { ssid[0] = pass[0] = 0; return; } scp(ssid, kn_ssid[i], 33); scp(pass, kn_pass[i], 64); }
void wifi_known_set(int i, const char *ssid, const char *pass) { if (i >= 0 && i < KNOWN_EXTRA) { scp(kn_ssid[i], ssid, 33); scp(kn_pass[i], pass, 64); } }

/* запомнить сеть: она встаёт на первое место, остальные сдвигаются (повтор имени убирается) */
static void remember(const char *ssid, const char *pass)
{
    char os[KNOWN_EXTRA + 1][33], op[KNOWN_EXTRA + 1][64]; int n = 0;
    scp(os[n], sv_ssid, 33); scp(op[n], sv_pass, 64); n++;
    for (int i = 0; i < KNOWN_EXTRA; i++) { scp(os[n], kn_ssid[i], 33); scp(op[n], kn_pass[i], 64); n++; }
    scp(sv_ssid, ssid, 33); scp(sv_pass, pass, 64);
    int k = 0;
    for (int i = 0; i < n && k < KNOWN_EXTRA; i++) {
        if (!os[i][0] || seq(os[i], ssid)) continue;
        scp(kn_ssid[k], os[i], 33); scp(kn_pass[k], op[i], 64); k++;
    }
    for (; k < KNOWN_EXTRA; k++) kn_ssid[k][0] = kn_pass[k][0] = 0;
}

static void forget(const char *ssid)                                /* убрать сеть из списка запомненных */
{
    char os[KNOWN_EXTRA][33], op[KNOWN_EXTRA][64]; int n = 0;
    for (int i = 0; i < KNOWN_EXTRA; i++) if (kn_ssid[i][0] && !seq(kn_ssid[i], ssid)) { scp(os[n], kn_ssid[i], 33); scp(op[n], kn_pass[i], 64); n++; }
    if (seq(sv_ssid, ssid)) {                                      /* забыли последнюю - на её место встаёт следующая */
        if (n) { scp(sv_ssid, os[0], 33); scp(sv_pass, op[0], 64); for (int i = 1; i < n; i++) { scp(os[i - 1], os[i], 33); scp(op[i - 1], op[i], 64); } n--; }
        else { sv_ssid[0] = sv_pass[0] = 0; }
    }
    for (int i = 0; i < KNOWN_EXTRA; i++) { if (i < n) { scp(kn_ssid[i], os[i], 33); scp(kn_pass[i], op[i], 64); } else kn_ssid[i][0] = kn_pass[i][0] = 0; }
}

/* пароль запомненной сети по имени или 0 */
static const char *known_pass(const char *ssid)
{
    if (sv_ssid[0] && seq(sv_ssid, ssid)) return sv_pass;
    for (int i = 0; i < KNOWN_EXTRA; i++) if (kn_ssid[i][0] && seq(kn_ssid[i], ssid)) return kn_pass[i];
    return 0;
}

static const sta_radio_t radio = { rtlwifi_tx80211, rtlwifi_set_channel, rtlwifi_set_bssid, rtlwifi_set_link };

int wifi_state(void) { return st; }
const char *wifi_adapter(void) { return st == WIFI_NONE ? "" : rtlwifi_name(); }

static void on_change(int s)
{
    if (s == STA_CONNECTED) {
        net_init(mac, sta_send_eth); connected_once = 1;
        if (try_ssid[0]) { remember(try_ssid, try_pass); prefs_flush(); }      /* запомнить сеть после удачного входа и сразу записать на диск */
        retries = 0;
    }
}

void wifi_refresh(void)
{
    if (probed) return;
    probed = 1;
    if (rtlwifi_probe()) { st = WIFI_OFF; msg = "Turn Wi-Fi on to start the adapter."; }
    else { st = WIFI_NONE; msg = "No supported Wi-Fi adapter was found."; }
}

void wifi_set_enabled(int on)
{
    if (st == WIFI_NONE || st == WIFI_STARTING) return;
    pref_on = on; boot_auto = 0;
    if (on) { st = WIFI_STARTING; msg = "Starting the adapter..."; pending = 1; frames = 0; }
    else { if (hw_ready) sta_disconnect(); st = WIFI_OFF; msg = "Wi-Fi is off."; }
}

void wifi_step(void)
{
    if (!pending || ++frames < 3) return;                            /* сначала нарисуем «запуск», потом работаем */
    pending = 0;
    if (hw_ready) { st = WIFI_ON; auto_tried = 0; sta_scan(); return; }
    int r = rtlwifi_have_blob() ? rtlwifi_start() : -12;
    if (r == 0) {
        rtlwifi_mac_get(mac);
        sta_init(mac, &radio); sta_set_callbacks(net_rx, on_change);
        hw_ready = 1; st = WIFI_ON; auto_tried = 0; sta_scan();
        return;
    }
    st = WIFI_ERROR;
    msg = r == -12 ? "Firmware file is missing. Start from the USB drive once and run: wifi fwinstall" :
          r == -13 ? "The adapter did not accept the firmware." :
          r == -14 ? "The firmware did not start." :
          r == -15 ? "Radio setup failed." :
          r == -11 ? "Buffer setup failed." : "The adapter did not respond.";
}

void wifi_tick(unsigned now)
{
    if (boot_auto && now > 400) {                                    /* Wi-Fi был включён до перезагрузки */
        boot_auto = 0; wifi_refresh();
        if (st == WIFI_OFF) { wifi_set_enabled(1); }
    }
    if (pending && boot_auto == 0 && st == WIFI_STARTING && !hw_ready) wifi_step();
    if (!hw_ready || !rtlwifi_started()) return;
    rtlwifi_poll(sta_rx);
    sta_tick(now);
    if (st == WIFI_ON && sv_ssid[0] && sta_state() != STA_CONNECTED && !wifi_busy() && !sta_scanning()) {
        if (!auto_tried && sta_net_count() > 0) {
            auto_tried = 1;                                          /* одна попытка за поиск: берём первую из запомненных, что видна, в порядке «свежести» */
            const char *order[KNOWN_EXTRA + 1]; int no = 0;
            order[no++] = sv_ssid;
            for (int i = 0; i < KNOWN_EXTRA; i++) if (kn_ssid[i][0]) order[no++] = kn_ssid[i];
            int done = 0;
            for (int o = 0; o < no && !done; o++)
                for (int i = 0; i < sta_net_count(); i++) {
                    const sta_net_t *n = sta_net(i);
                    if (!seq(n->ssid, order[o]) || n->sec == 2) continue;
                    scp(try_ssid, order[o], 33); scp(try_pass, known_pass(order[o]), 64);
                    sta_connect(i, try_pass);
                    done = 1; break;
                }
            retry_at = now + RETRY_TICKS;
        } else if (auto_tried && retries < RETRY_MAX && (int)(now - retry_at) >= 0) {
            retries++; auto_tried = 0; retry_at = now + RETRY_TICKS; sta_scan();      /* не вышло (сети не видно, роутер ещё грузится, обрыв): ищем снова */
        }
    }
}

void wifi_scan(void) { if (hw_ready && st == WIFI_ON) { auto_tried = 1; sta_scan(); } }
int wifi_busy(void) { int s = sta_state(); return s == STA_SCANNING || (s >= STA_AUTH && s < STA_CONNECTED); }

/* список без повторов имён, по убыванию сигнала */
static void rebuild(void)
{
    nord = 0;
    if (!hw_ready) return;
    int n = sta_net_count();
    for (int i = 0; i < n; i++) {
        const sta_net_t *a = sta_net(i);
        int dup = -1;
        for (int k = 0; k < nord; k++) { const sta_net_t *b = sta_net(ord[k]); int j = 0; while (a->ssid[j] && a->ssid[j] == b->ssid[j]) j++; if (a->ssid[j] == b->ssid[j]) { dup = k; break; } }
        if (dup >= 0) { if (a->rssi > sta_net(ord[dup])->rssi) ord[dup] = i; continue; }
        ord[nord++] = i;
    }
    for (int i = 1; i < nord; i++) { int v = ord[i], j = i; while (j > 0 && sta_net(ord[j - 1])->rssi < sta_net(v)->rssi) { ord[j] = ord[j - 1]; j--; } ord[j] = v; }
    int c = sta_connected_net();
    if (c >= 0) for (int i = 0; i < nord; i++) if (ord[i] != c) { const sta_net_t *a = sta_net(c), *b = sta_net(ord[i]); int j = 0; while (a->ssid[j] && a->ssid[j] == b->ssid[j]) j++; if (a->ssid[j] == b->ssid[j]) ord[i] = c; }
}
int wifi_net_count(void) { rebuild(); return nord; }
const char *wifi_net_name(int i) { return i >= 0 && i < nord ? sta_net(ord[i])->ssid : ""; }
int wifi_net_signal(int i) { if (i < 0 || i >= nord) return 0; int r = sta_net(ord[i])->rssi; return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1; }
int wifi_net_secure(int i) { return i >= 0 && i < nord ? sta_net(ord[i])->sec : 0; }
int wifi_net_connected(int i) { return i >= 0 && i < nord && sta_connected_net() == ord[i]; }

int wifi_connect(int i, const char *pass)
{
    if (i < 0 || i >= nord) return -1;
    sel_cur = ord[i];
    scp(try_ssid, sta_net(ord[i])->ssid, 33); scp(try_pass, pass, 64);
    return sta_connect(ord[i], pass);
}
void wifi_disconnect(void)                                         /* вручную отключили - забыть эту сеть (остальные запомненные остаются) */
{
    if (try_ssid[0]) forget(try_ssid); else forget(sv_ssid);
    try_ssid[0] = 0; retries = RETRY_MAX;
    if (hw_ready) sta_disconnect();
    prefs_flush();
}

static char *put_str(char *p, const char *a) { while (*a) *p++ = *a++; return p; }
static char *put_num(char *p, unsigned x) { char r[12]; int l = 0; do { r[l++] = (char)('0' + x % 10); x /= 10; } while (x); while (l) *p++ = r[--l]; return p; }

const char *wifi_message(void)
{
    if (st != WIFI_ON) return msg;
    int s = sta_state();
    if (s == STA_CONNECTED) {
        net_info_t ni; net_info(&ni);
        char *p = info; const char *a = "Connected to ";
        while (*a) *p++ = *a++;
        const char *n = sta_net(sta_connected_net() >= 0 ? sta_connected_net() : 0)->ssid; while (*n) *p++ = *n++;
        if (ni.up) { a = "  -  "; while (*a) *p++ = *a++; char ip[20]; net_ip_fmt(ni.ip, ip); for (char *q = ip; *q; ) *p++ = *q++; }
        else { a = "  -  getting an address..."; while (*a) *p++ = *a++; }
        *p = 0; return info;
    }
    if ((s == STA_IDLE || s == STA_SCANNING) && !sta_net_count()) {     /* счётчики радио прямо на экране: видно, слышит ли чип эфир */
        unsigned v[6]; rtlwifi_rxstat(v);
        char *p = info; p = put_str(p, "Searching... sent "); p = put_num(p, v[3]);
        p = put_str(p, ", heard "); p = put_num(p, v[0]); p = put_str(p, ", bad "); p = put_num(p, v[1]);
        *p = 0; return info;
    }
    if (s == STA_IDLE) return "Choose a network.";
    return sta_text();
}
