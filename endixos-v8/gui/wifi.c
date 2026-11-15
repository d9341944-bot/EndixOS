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

static void scp(char *d, const char *s, int cap) { int i = 0; while (s && s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int seq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
void wifi_pref_get(char *ssid, char *pass, int *on) { scp(ssid, sv_ssid, 33); scp(pass, sv_pass, 64); *on = pref_on; }
void wifi_pref_set(const char *ssid, const char *pass, int on) { scp(sv_ssid, ssid, 33); scp(sv_pass, pass, 64); pref_on = on; boot_auto = on; }

static const sta_radio_t radio = { rtlwifi_tx80211, rtlwifi_set_channel, rtlwifi_set_bssid, rtlwifi_set_link };

int wifi_state(void) { return st; }
const char *wifi_adapter(void) { return st == WIFI_NONE ? "" : rtlwifi_name(); }

static void on_change(int s)
{
    if (s == STA_CONNECTED) {
        net_init(mac, sta_send_eth); connected_once = 1;
        if (try_ssid[0]) { scp(sv_ssid, try_ssid, 33); scp(sv_pass, try_pass, 64); }      /* запомнить сеть после удачного входа */
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
    if (st == WIFI_ON && sv_ssid[0] && !auto_tried && sta_state() == STA_IDLE && !sta_scanning() && sta_net_count() > 0) {
        auto_tried = 1;                                              /* одна попытка за поиск */
        for (int i = 0; i < sta_net_count(); i++) {
            const sta_net_t *n = sta_net(i);
            if (!seq(n->ssid, sv_ssid) || n->sec == 2) continue;
            scp(try_ssid, sv_ssid, 33); scp(try_pass, sv_pass, 64);
            sta_connect(i, sv_pass);
            break;
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
void wifi_disconnect(void) { sv_ssid[0] = sv_pass[0] = try_ssid[0] = 0; if (hw_ready) sta_disconnect(); }   /* вручную отключили - забыть сеть */

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
