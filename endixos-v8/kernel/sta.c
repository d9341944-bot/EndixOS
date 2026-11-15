#include "sta.h"
#include "crypto.h"

static void scopy(void *d, const void *s, u32 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void szero(void *d, u32 n) { u8 *a = d; while (n--) *a++ = 0; }
static int  meq(const void *a, const void *b, u32 n) { const u8 *x = a, *y = b; while (n--) if (*x++ != *y++) return 0; return 1; }

static u8 mymac[6]; static sta_radio_t radio;
static void (*eth_cb)(const u8 *, u32); static void (*chg_cb)(int);
static sta_net_t nets[STA_MAX_NETS]; static int nnets;
static int st = STA_IDLE; static const char *text = "";
static u32 now, deadline, dwell_start; static int scan_ch, tries, cur = -1;
static u16 seq;

/* текущее соединение */
static u8 bssid[6], pmk[32], ptk[48], snonce[32], anonce[32], gtk[4][16], have_gtk[4];
static u8 have_ptk, secure, passlen, pass[64], rsn_ie_out[22];
static u32 tx_pn_lo, tx_pn_hi; static u8 last_replay[8], have_replay;
static char want_ssid[33];

static const u8 bcast[6] = { 255, 255, 255, 255, 255, 255 };
static const u8 rates[] = { 1, 8, 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24 };       /* IE 1: 1, 2, 5.5, 11, 6, 9, 12, 18 */
static const u8 xrates[] = { 50, 4, 0x30, 0x48, 0x60, 0x6c };                                /* IE 50: 24, 36, 48, 54 */
static const u8 our_rsn[22] = { 0x30, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0, 0 };

static void set_state(int s, const char *t) { st = s; text = t; if (chg_cb) chg_cb(s); }

void sta_init(const u8 m[6], const sta_radio_t *r) { scopy(mymac, m, 6); radio = *r; st = STA_IDLE; text = ""; nnets = 0; cur = -1; }
void sta_set_callbacks(void (*e)(const u8 *, u32), void (*c)(int)) { eth_cb = e; chg_cb = c; }
int sta_state(void) { return st; }
const char *sta_text(void) { return text; }
int sta_net_count(void) { return nnets; }
const sta_net_t *sta_net(int i) { return i >= 0 && i < nnets ? &nets[i] : 0; }
int sta_connected_net(void) { return st == STA_CONNECTED ? cur : -1; }
int sta_scanning(void) { return st == STA_SCANNING; }

/* ---------- построение кадров ---------- */
static u8 *mgmt_hdr(u8 *b, u16 fc, const u8 *da, const u8 *bss)
{
    b[0] = (u8)fc; b[1] = (u8)(fc >> 8); b[2] = b[3] = 0;
    scopy(b + 4, da, 6); scopy(b + 10, mymac, 6); scopy(b + 16, bss, 6);
    u16 s = (u16)(seq++ << 4); b[22] = (u8)s; b[23] = (u8)(s >> 8);
    return b + 24;
}
static u8 *put_ie(u8 *p, u8 id, const void *d, u8 n) { p[0] = id; p[1] = n; scopy(p + 2, d, n); return p + 2 + n; }
static u8 *put_ssid(u8 *p, const char *s) { u8 n = 0; while (s[n] && n < 32) n++; return put_ie(p, 0, s, n); }

static void send_probe(void)
{
    u8 b[96]; u8 *p = mgmt_hdr(b, 0x0040, bcast, bcast);
    p = put_ssid(p, "");
    scopy(p, rates, sizeof rates); p += sizeof rates;
    scopy(p, xrates, sizeof xrates); p += sizeof xrates;
    radio.tx(b, (u32)(p - b));
}
static void send_auth(void)
{
    u8 b[64]; u8 *p = mgmt_hdr(b, 0x00B0, bssid, bssid);
    p[0] = 0; p[1] = 0; p[2] = 1; p[3] = 0; p[4] = 0; p[5] = 0; p += 6;       /* open system, seq 1, status 0 */
    radio.tx(b, (u32)(p - b));
}
static void send_assoc(void)
{
    u8 b[160]; u8 *p = mgmt_hdr(b, 0x0000, bssid, bssid);
    u16 cap = (u16)(0x0001 | 0x0020 | 0x0400 | (secure ? 0x0010 : 0));
    p[0] = (u8)cap; p[1] = (u8)(cap >> 8); p[2] = 10; p[3] = 0; p += 4;       /* возможности, интервал прослушивания */
    p = put_ssid(p, want_ssid);
    scopy(p, rates, sizeof rates); p += sizeof rates;
    scopy(p, xrates, sizeof xrates); p += sizeof xrates;
    if (secure) { scopy(p, our_rsn, 22); p += 22; }
    radio.tx(b, (u32)(p - b));
}
static void send_deauth(void)
{
    u8 b[32]; u8 *p = mgmt_hdr(b, 0x00C0, bssid, bssid);
    p[0] = 3; p[1] = 0; p += 2;
    radio.tx(b, (u32)(p - b));
}

/* ---------- данные и CCMP ---------- */
static u32 ccmp_aad(u8 *aad, const u8 *h, int qos)
{
    u16 fc = (u16)(h[0] | h[1] << 8);
    fc = (u16)((fc & ~0x3800) | 0x4000);
    aad[0] = (u8)fc; aad[1] = (u8)(fc >> 8);
    scopy(aad + 2, h + 4, 18);                                                 /* A1 A2 A3 */
    aad[20] = (u8)(h[22] & 0x0f); aad[21] = 0;
    if (!qos) return 22;
    aad[22] = (u8)(h[24] & 0x0f); aad[23] = 0;
    return 24;
}
static void ccmp_nonce(u8 *n, u8 prio, const u8 *a2, const u8 *pnh)          /* pnh: PN5..PN0 (старший первым) */
{
    n[0] = prio; scopy(n + 1, a2, 6); scopy(n + 7, pnh, 6);
}

static int send_data(const u8 *da, u16 ethertype, const u8 *pay, u32 n, int encrypt)
{
    static u8 f[1600], body[8 + 1500];
    u32 maxp = 1500;
    if (n > maxp) return -1;
    f[0] = (u8)(0x0108 | (encrypt ? 0x4000 : 0)); f[1] = (u8)((0x0108 | (encrypt ? 0x4000 : 0)) >> 8);
    f[2] = f[3] = 0;
    scopy(f + 4, bssid, 6); scopy(f + 10, mymac, 6); scopy(f + 16, da, 6);
    u16 s = (u16)(seq++ << 4); f[22] = (u8)s; f[23] = (u8)(s >> 8);
    body[0] = 0xAA; body[1] = 0xAA; body[2] = 3; body[3] = body[4] = body[5] = 0; body[6] = (u8)(ethertype >> 8); body[7] = (u8)ethertype;
    scopy(body + 8, pay, n);
    u32 bl = 8 + n, len;
    if (!encrypt) { scopy(f + 24, body, bl); len = 24 + bl; }
    else {
        if (++tx_pn_lo == 0) tx_pn_hi++;
        u8 pnh[6] = { (u8)(tx_pn_hi >> 8), (u8)tx_pn_hi, (u8)(tx_pn_lo >> 24), (u8)(tx_pn_lo >> 16), (u8)(tx_pn_lo >> 8), (u8)tx_pn_lo };
        u8 nonce[13], aad[24], mic[8];
        ccmp_nonce(nonce, 0, mymac, pnh);
        u32 al = ccmp_aad(aad, f, 0);
        f[24] = pnh[5]; f[25] = pnh[4]; f[26] = 0; f[27] = 0x20; f[28] = pnh[3]; f[29] = pnh[2]; f[30] = pnh[1]; f[31] = pnh[0];
        ccmp_seal(ptk + 32, nonce, aad, al, body, bl, f + 32, mic);
        scopy(f + 32 + bl, mic, 8);
        len = 32 + bl + 8;
    }
    radio.tx(f, len);
    return 0;
}

int sta_send_eth(const void *frame, u32 len)
{
    const u8 *e = frame;
    if (st != STA_CONNECTED || len < 14) return -1;
    return send_data(e, (u16)(e[12] << 8 | e[13]), e + 14, len - 14, secure && have_ptk);
}

/* ---------- EAPOL ---------- */
static void eapol_mic(const u8 *kck, u8 *eapol, u32 len)                      /* вписывает MIC в кадр */
{
    u8 h[20];
    szero(eapol + 81, 16);
    hmac_sha1(kck, 16, eapol, len, h);
    scopy(eapol + 81, h, 16);
}
static void send_eapol_key(u16 info, u16 keylen, const u8 *replay, const u8 *nonce, const u8 *data, u16 dl)
{
    u8 e[128 + 4]; u32 len = 99 + dl;
    szero(e, len);
    e[0] = 2; e[1] = 3; e[2] = (u8)((len - 4) >> 8); e[3] = (u8)(len - 4);
    e[4] = 2; e[5] = (u8)(info >> 8); e[6] = (u8)info; e[7] = (u8)(keylen >> 8); e[8] = (u8)keylen;
    scopy(e + 9, replay, 8);
    if (nonce) scopy(e + 17, nonce, 32);
    e[97] = (u8)(dl >> 8); e[98] = (u8)dl;
    if (dl) scopy(e + 99, data, dl);
    eapol_mic(ptk, e, len);
    send_data(bssid, 0x888e, e, len, have_ptk && secure && (info & 0x0200) && (info & 0x0008) == 0);
}

static void install_gtk(const u8 *kde, u32 n)
{
    for (u32 i = 0; i + 2 <= n;) {
        u8 id = kde[i], l = kde[i + 1];
        if (id == 0 && l == 0) break;
        if (i + 2 + l > n) break;
        if (id == 0xDD && l >= 6 + 16 && kde[i+2] == 0x00 && kde[i+3] == 0x0f && kde[i+4] == 0xac && kde[i+5] == 1) {
            int k = kde[i + 6] & 3;
            scopy(gtk[k], kde + i + 8, 16); have_gtk[k] = 1;
        }
        i += 2 + (u32)l;
    }
}

static int replay_gt(const u8 *a, const u8 *b) { for (int i = 0; i < 8; i++) { if (a[i] != b[i]) return a[i] > b[i]; } return 0; }

static void handle_eapol(const u8 *e, u32 n)
{
    if (n < 99 || e[1] != 3 || e[4] != 2) return;
    u16 info = (u16)(e[5] << 8 | e[6]);
    u16 dl = (u16)(e[97] << 8 | e[98]);
    if (99u + dl > n || dl > 120) return;
    const u8 *replay = e + 9;
    if ((info & 0x0008) && (info & 0x0080) && !(info & 0x0100)) {               /* пара ключей, M1 */
        if (st != STA_HANDSHAKE || !secure) return;
        scopy(anonce, e + 17, 32);
        rng_fill(snonce, 32);
        u8 data[64], x[76];
        const u8 *a = bssid, *b = mymac;
        int c = 0; for (int i = 0; i < 6; i++) if (a[i] != b[i]) { c = a[i] > b[i] ? 1 : -1; break; }
        scopy(x, c < 0 ? a : b, 6); scopy(x + 6, c < 0 ? b : a, 6);
        c = 0; for (int i = 0; i < 32; i++) if (anonce[i] != snonce[i]) { c = anonce[i] > snonce[i] ? 1 : -1; break; }
        scopy(x + 12, c < 0 ? anonce : snonce, 32); scopy(x + 44, c < 0 ? snonce : anonce, 32);
        wpa_prf(pmk, 32, "Pairwise key expansion", x, 76, ptk, 48);
        scopy(last_replay, replay, 8); have_replay = 1;
        scopy(data, our_rsn, 22);
        have_ptk = 0;
        send_eapol_key(0x010a, 0, replay, snonce, data, 22);
        return;
    }
    if ((info & 0x0008) && (info & 0x0100) && (info & 0x0080)) {                /* M3 */
        if (st != STA_HANDSHAKE && st != STA_CONNECTED) return;
        u8 tmp[160]; scopy(tmp, e, 99 + dl);
        u8 mic[16]; scopy(mic, e + 81, 16);
        eapol_mic(ptk, tmp, 99u + dl);
        if (!meq(tmp + 81, mic, 16)) return;                                     /* MIC не сошёлся - пароль или чужой кадр */
        if (have_replay && !replay_gt(replay, last_replay) && !meq(replay, last_replay, 8)) return;
        scopy(last_replay, replay, 8);
        if ((info & 0x1000) && dl >= 16 && dl % 8 == 0) {
            u8 kd[120];
            if (aes_key_unwrap(ptk + 16, e + 99, dl / 8, kd)) install_gtk(kd, dl - 8);
        }
        send_eapol_key(0x030a, 0, replay, 0, 0, 0);                              /* M4 уходит открыто, ключ ставим после */
        have_ptk = 1;
        if (st == STA_HANDSHAKE) { radio.set_link(1); set_state(STA_CONNECTED, "Connected"); }
        return;
    }
    if (!(info & 0x0008) && (info & 0x0100) && (info & 0x0080)) {               /* смена ключа группы */
        if (!have_ptk) return;
        u8 tmp[160]; scopy(tmp, e, 99 + dl);
        u8 mic[16]; scopy(mic, e + 81, 16);
        eapol_mic(ptk, tmp, 99u + dl);
        if (!meq(tmp + 81, mic, 16)) return;
        if ((info & 0x1000) && dl >= 16 && dl % 8 == 0) {
            u8 kd[120];
            if (aes_key_unwrap(ptk + 16, e + 99, dl / 8, kd)) install_gtk(kd, dl - 8);
        }
        u8 pad[8]; (void)pad;
        send_eapol_key(0x0302, 0, replay, 0, 0, 0);
    }
}

/* ---------- приём ---------- */
static const u8 *ie_find(const u8 *p, const u8 *e, u8 id)
{
    while (p + 2 <= e) { if (p + 2 + p[1] > e) return 0; if (p[0] == id) return p; p += 2 + p[1]; }
    return 0;
}
static int suite_is(const u8 *s, u8 t) { return s[0] == 0x00 && s[1] == 0x0f && s[2] == 0xac && s[3] == t; }

static int rsn_security(const u8 *ie)                                            /* ie: начиная с id 0x30 */
{
    u32 l = ie[1];
    if (l < 8 || ie[2] != 1) return SEC_UNSUPPORTED;
    const u8 *p = ie + 4; const u8 *e = ie + 2 + l;
    if (!suite_is(p, 4)) return SEC_UNSUPPORTED;                                 /* группа не CCMP (TKIP, WEP) */
    p += 4;
    if (p + 2 > e) return SEC_UNSUPPORTED;
    u32 pc = (u32)(p[0] | p[1] << 8); p += 2;
    int ccmp = 0; for (u32 i = 0; i < pc && p + 4 <= e; i++, p += 4) if (suite_is(p, 4)) ccmp = 1;
    if (!ccmp || p + 2 > e) return SEC_UNSUPPORTED;
    u32 ac = (u32)(p[0] | p[1] << 8); p += 2;
    int psk = 0; for (u32 i = 0; i < ac && p + 4 <= e; i++, p += 4) if (suite_is(p, 2)) psk = 1;
    if (!psk) return SEC_UNSUPPORTED;
    if (p + 2 <= e && (p[0] & 0x40)) return SEC_UNSUPPORTED;                     /* защита управляющих кадров обязательна */
    return SEC_WPA2;
}

static void on_beacon(const u8 *f, u32 n, int rssi, int ch_hint)
{
    if (n < 36) return;
    const u8 *p = f + 36, *e = f + n;
    u16 cap = (u16)(f[34] | f[35] << 8);
    const u8 *s = ie_find(p, e, 0);
    if (!s || s[1] == 0 || s[1] > 32) return;
    char ssid[33]; scopy(ssid, s + 2, s[1]); ssid[s[1]] = 0;
    for (int i = 0; ssid[i]; i++) if ((u8)ssid[i] < 32) return;
    const u8 *d = ie_find(p, e, 3);
    int ch = d && d[1] >= 1 ? d[2] : ch_hint;
    if (ch < 1 || ch > 14) return;
    const u8 *r = ie_find(p, e, 0x30);
    int sec = r ? rsn_security(r) : ((cap & 0x10) ? SEC_UNSUPPORTED : SEC_OPEN);
    for (int i = 0; i < nnets; i++)
        if (meq(nets[i].bssid, f + 16, 6)) { if (rssi > nets[i].rssi) nets[i].rssi = (i8)rssi; return; }
    if (nnets >= STA_MAX_NETS) return;
    sta_net_t *nn = &nets[nnets++];
    scopy(nn->ssid, ssid, 33); scopy(nn->bssid, f + 16, 6); nn->channel = (u8)ch; nn->rssi = (i8)rssi; nn->sec = (u8)sec;
}

static void fail(const char *t) { have_ptk = 0; radio.set_link(0); radio.set_bssid(0); set_state(STA_FAILED, t); }

void sta_rx(const u8 *f, u32 n, int rssi)
{
    if (n < 24) return;
    u16 fc = (u16)(f[0] | f[1] << 8);
    int type = (fc >> 2) & 3, sub = (fc >> 4) & 15;
    if (type == 0) {                                                             /* управление */
        if ((sub == 8 || sub == 5) && st == STA_SCANNING) { on_beacon(f, n, rssi, scan_ch); return; }
        if (!meq(f + 16, bssid, 6) || st == STA_IDLE || st == STA_SCANNING || st == STA_FAILED) return;
        if (sub == 11 && st == STA_AUTH && n >= 30) {                            /* ответ на авторизацию */
            if (f[28] == 0 && f[29] == 0) { set_state(STA_ASSOC, "Joining the network..."); tries = 0; deadline = now + 100; send_assoc(); }
            else fail("The network refused the connection.");
        } else if (sub == 1 && st == STA_ASSOC && n >= 30) {                     /* ответ на ассоциацию */
            if (f[26] == 0 && f[27] == 0) {
                if (!secure) { radio.set_link(1); set_state(STA_CONNECTED, "Connected"); }
                else { have_ptk = 0; have_replay = 0; tx_pn_lo = tx_pn_hi = 0; for (int i = 0; i < 4; i++) have_gtk[i] = 0; deadline = now + 500; set_state(STA_HANDSHAKE, "Checking the password..."); }
            } else fail("The network refused the connection.");
        } else if (sub == 12 || sub == 10) {                                     /* deauth / disassoc */
            if (st == STA_HANDSHAKE) fail("Incorrect password."); else fail("Disconnected by the network.");
        }
        return;
    }
    if (type != 2 || st < STA_ASSOC || (fc & 0x0300) != 0x0200 || (sub & 4)) return;       /* данные от точки, не null */
    if (!meq(f + 10, bssid, 6)) return;
    int qos = sub & 8; u32 hl = 24 + (qos ? 2u : 0) + ((qos && (fc & 0x8000)) ? 4u : 0);
    int mc = f[4] & 1;
    if (!mc && !meq(f + 4, mymac, 6)) return;
    if (meq(f + 16, mymac, 6)) return;                                           /* своё же эхо */
    static u8 plain[1600], eth[14 + 1500]; const u8 *body; u32 bl;
    if (fc & 0x4000) {
        if (n < hl + 16 || !(f[hl + 3] & 0x20) || n > 1600) return;
        int kid = f[hl + 3] >> 6;
        const u8 *key = mc ? (have_gtk[kid] ? gtk[kid] : 0) : (have_ptk ? ptk + 32 : 0);
        if (!key) return;
        u8 pnh[6] = { f[hl + 7], f[hl + 6], f[hl + 5], f[hl + 4], f[hl + 1], f[hl] };
        u8 nonce[13], aad[24];
        ccmp_nonce(nonce, qos ? (u8)(f[24] & 0x0f) : 0, f + 10, pnh);
        u32 al = ccmp_aad(aad, f, qos ? 1 : 0);
        bl = n - hl - 16;
        if (!ccmp_open(key, nonce, aad, al, f + hl + 8, bl, plain, f + n - 8)) return;
        body = plain;
    } else {
        if (n < hl + 8) return;
        body = f + hl; bl = n - hl;
        if (secure && !(body[6] == 0x88 && body[7] == 0x8e)) return;            /* в защищённой сети открытым идёт только EAPOL */
    }
    if (bl < 8 || body[0] != 0xAA || body[1] != 0xAA || body[2] != 3) return;
    u16 et = (u16)(body[6] << 8 | body[7]);
    if (et == 0x888e) { handle_eapol(body + 8, bl - 8); return; }
    if (st != STA_CONNECTED || !eth_cb || bl - 8 > 1500) return;
    scopy(eth, f + 4, 6); scopy(eth + 6, f + 16, 6); eth[12] = body[6]; eth[13] = body[7];
    scopy(eth + 14, body + 8, bl - 8);
    eth_cb(eth, 14 + bl - 8);
}

/* ---------- управление ---------- */
void sta_scan(void)
{
    if (st == STA_SCANNING || (st >= STA_AUTH && st <= STA_CONNECTED)) return;
    nnets = 0; cur = -1; scan_ch = 1; radio.set_bssid(0); radio.set_link(0);
    radio.set_channel(1); send_probe(); dwell_start = now;
    set_state(STA_SCANNING, "Searching for networks...");
}

int sta_connect(int idx, const char *pw)
{
    if (idx < 0 || idx >= nnets || nets[idx].sec == SEC_UNSUPPORTED) return -1;
    if (st >= STA_AUTH && st <= STA_CONNECTED) return -1;
    cur = idx; secure = nets[idx].sec == SEC_WPA2;
    scopy(bssid, nets[idx].bssid, 6); scopy(want_ssid, nets[idx].ssid, 33);
    if (secure) {
        u32 pl = 0; while (pw && pw[pl] && pl < 63) pl++;
        if (pl < 8) return -2;
        u32 sl = 0; while (want_ssid[sl]) sl++;
        pbkdf2_sha1((const u8 *)pw, pl, (const u8 *)want_ssid, sl, 4096, pmk, 32);
    }
    have_ptk = 0; tx_pn_lo = tx_pn_hi = 0;
    radio.set_channel(nets[idx].channel); radio.set_bssid(bssid);
    tries = 0; deadline = now + 100; set_state(STA_AUTH, "Connecting...");
    send_auth();
    return 0;
}

void sta_disconnect(void)
{
    if (st == STA_CONNECTED || (st >= STA_AUTH && st <= STA_HANDSHAKE)) send_deauth();
    have_ptk = 0; radio.set_link(0); radio.set_bssid(0); cur = -1;
    set_state(STA_IDLE, "");
}

void sta_tick(u32 t)
{
    now = t;
    if (st == STA_SCANNING) {
        if (now - dwell_start >= 5) {
            if (++scan_ch > 13) { set_state(STA_IDLE, nnets ? "" : "No networks found."); return; }
            radio.set_channel(scan_ch); send_probe(); dwell_start = now;
        }
    } else if ((st == STA_AUTH || st == STA_ASSOC) && (i32)(now - deadline) >= 0) {
        if (++tries >= 3) { fail("The network did not answer."); return; }
        deadline = now + 100;
        if (st == STA_AUTH) send_auth(); else send_assoc();
    } else if (st == STA_HANDSHAKE && (i32)(now - deadline) >= 0) {
        fail("Incorrect password.");
    }
}
