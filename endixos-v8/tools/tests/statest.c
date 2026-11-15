#include "kernel/crypto.c"
#include "kernel/sta.c"
extern int printf(const char *, ...); extern int memcmp(const void *, const void *, unsigned long); extern unsigned long strlen(const char *);
static int fails; static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }
#define CHECK(c, m) do { if (c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); fails++; } } while (0)

static u8 AP[6] = { 0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01 }, ME[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const char *PASS = "correct horse", *SSID = "TestNet";
static u8 txq[16][1700]; static u32 txl[16]; static int txn, cur_ch, link_up, got_bssid;
static void r_tx(const u8 *f, u32 n) { if (txn < 16) { for (u32 i = 0; i < n; i++) txq[txn][i] = f[i]; txl[txn++] = n; } }
static void r_ch(int c) { cur_ch = c; }
static void r_bssid(const u8 *b) { got_bssid = b != 0; }
static void r_link(int up) { link_up = up; }
static u8 eth_got[4][1600]; static u32 eth_len[4]; static int eth_n;
static void on_eth(const u8 *f, u32 n) { if (eth_n < 4) { for (u32 i = 0; i < n; i++) eth_got[eth_n][i] = f[i]; eth_len[eth_n++] = n; } }

/* ---- модель точки доступа ---- */
static u8 ap_pmk[32], ap_ptk[48], ap_anonce[32], ap_snonce[32], ap_gtk[16], ap_replay[8]; static int ap_ptk_ok, ap_pn, ap_state, ap_wrong;
static void inject(const u8 *f, u32 n) { sta_rx(f, n, -50); }
static void ap_mgmt(u16 fc, const u8 *da, u8 *b, u32 *n, const u8 *body, u32 bl)
{
    b[0] = (u8)fc; b[1] = (u8)(fc >> 8); b[2] = b[3] = 0;
    for (int i = 0; i < 6; i++) { b[4 + i] = da[i]; b[10 + i] = AP[i]; b[16 + i] = AP[i]; }
    b[22] = b[23] = 0;
    for (u32 i = 0; i < bl; i++) b[24 + i] = body[i];
    *n = 24 + bl;
}
static u32 beacon_body(u8 *p, const char *ssid, int ch, int kind)
{
    u8 *s = p; for (int i = 0; i < 8; i++) *p++ = 0; *p++ = 100; *p++ = 0;
    u16 cap = kind == 0 ? 0x0401 : 0x0411; *p++ = (u8)cap; *p++ = (u8)(cap >> 8);
    *p++ = 0; *p++ = (u8)strlen(ssid); for (const char *c = ssid; *c; c++) *p++ = (u8)*c;
    *p++ = 3; *p++ = 1; *p++ = (u8)ch;
    if (kind == 1) { static const u8 r[] = { 0x30, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0, 0 }; for (u32 i = 0; i < sizeof r; i++) *p++ = r[i]; }
    if (kind == 2) { static const u8 r[] = { 0x30, 20, 1, 0, 0, 0x0f, 0xac, 2, 1, 0, 0, 0x0f, 0xac, 2, 1, 0, 0, 0x0f, 0xac, 2, 0, 0 }; for (u32 i = 0; i < sizeof r; i++) *p++ = r[i]; }   /* TKIP */
    if (kind == 3) { static const u8 r[] = { 0x30, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 8, 0, 0 }; for (u32 i = 0; i < sizeof r; i++) *p++ = r[i]; }   /* WPA3 SAE */
    return (u32)(p - s);
}
static void ap_beacon(const char *ssid, int ch, int kind, u8 last)
{
    u8 b[200], body[150]; u32 n; u32 bl = beacon_body(body, ssid, ch, kind);
    u8 save = AP[5]; AP[5] = last;
    ap_mgmt(0x0050, ME, b, &n, body, bl); inject(b, n); AP[5] = save;
}
static u32 ap_data_out(u8 *f, const u8 *da, const u8 *sa, u16 et, const u8 *pay, u32 pl, int enc, const u8 *key, int bc)
{
    u16 fc = (u16)(0x0208 | (enc ? 0x4000 : 0));
    f[0] = (u8)fc; f[1] = (u8)(fc >> 8); f[2] = f[3] = 0;
    for (int i = 0; i < 6; i++) { f[4 + i] = da[i]; f[10 + i] = AP[i]; f[16 + i] = sa[i]; }
    f[22] = f[23] = 0;
    u8 body[1600]; body[0] = body[1] = 0xAA; body[2] = 3; body[3] = body[4] = body[5] = 0; body[6] = (u8)(et >> 8); body[7] = (u8)et;
    for (u32 i = 0; i < pl; i++) body[8 + i] = pay[i];
    u32 bl = 8 + pl;
    if (!enc) { for (u32 i = 0; i < bl; i++) f[24 + i] = body[i]; return 24 + bl; }
    ap_pn++;
    u8 pnh[6] = { 0, 0, 0, 0, (u8)(ap_pn >> 8), (u8)ap_pn }, nonce[13], aad[24], mic[8];
    nonce[0] = 0; for (int i = 0; i < 6; i++) nonce[1 + i] = AP[i]; for (int i = 0; i < 6; i++) nonce[7 + i] = pnh[i];
    aad[0] = (u8)(fc & ~0x3800); aad[1] = (u8)((fc & ~0x3800) >> 8); for (int i = 0; i < 18; i++) aad[2 + i] = f[4 + i]; aad[20] = 0; aad[21] = 0;
    f[24] = pnh[5]; f[25] = pnh[4]; f[26] = 0; f[27] = (u8)(0x20 | (bc ? 1 << 6 : 0)); f[28] = pnh[3]; f[29] = pnh[2]; f[30] = pnh[1]; f[31] = pnh[0];
    ccmp_seal(key, nonce, aad, 22, body, bl, f + 32, mic);
    for (int i = 0; i < 8; i++) f[32 + bl + i] = mic[i];
    return 32 + bl + 8;
}
static void ap_send_eapol(u16 info, const u8 *nonce, const u8 *data, u16 dl, u16 keylen)
{
    u8 e[300]; u32 len = 99 + dl;
    for (u32 i = 0; i < len; i++) e[i] = 0;
    e[0] = 2; e[1] = 3; e[2] = (u8)((len - 4) >> 8); e[3] = (u8)(len - 4); e[4] = 2; e[5] = (u8)(info >> 8); e[6] = (u8)info; e[8] = (u8)keylen;
    for (int i = 0; i < 8; i++) e[9 + i] = ap_replay[i];
    if (nonce) for (int i = 0; i < 32; i++) e[17 + i] = nonce[i];
    e[97] = (u8)(dl >> 8); e[98] = (u8)dl;
    for (u32 i = 0; i < dl; i++) e[99 + i] = data[i];
    if (info & 0x0100) { u8 h[20]; hmac_sha1(ap_ptk, 16, e, len, h); for (int i = 0; i < 16; i++) e[81 + i] = h[i]; }
    u8 f[600]; u32 n = ap_data_out(f, ME, AP, 0x888e, e, len, 0, 0, 0);
    inject(f, n);
}
static void ap_wrap(const u8 *kek, const u8 *p, u32 n, u8 *out)                     /* RFC 3394 */
{
    aes128_t k; aes128_set_key(&k, kek);
    u8 a[8], b[16], r[64];
    for (int i = 0; i < 8; i++) a[i] = 0xA6;
    for (u32 i = 0; i < n * 8; i++) r[i] = p[i];
    for (int j = 0; j < 6; j++) for (u32 i = 1; i <= n; i++) {
        for (int x = 0; x < 8; x++) { b[x] = a[x]; b[8 + x] = r[(i - 1) * 8 + x]; }
        aes128_encrypt(&k, b, b);
        u32 t = n * (u32)j + i;
        for (int x = 0; x < 8; x++) a[x] = b[x];
        a[7] ^= (u8)t; a[6] ^= (u8)(t >> 8);
        for (int x = 0; x < 8; x++) r[(i - 1) * 8 + x] = b[8 + x];
    }
    for (int x = 0; x < 8; x++) out[x] = a[x];
    for (u32 x = 0; x < n * 8; x++) out[8 + x] = r[x];
}
static void ap_respond_all(void)
{
    for (int q = 0; q < txn; q++) {
        u8 *f = txq[q]; u32 n = txl[q]; u16 fc = (u16)(f[0] | f[1] << 8); int type = (fc >> 2) & 3, sub = (fc >> 4) & 15;
        u8 b[400]; u32 ol;
        if (type == 0 && sub == 4) {                                              /* probe: ответить на канале точки */
            if (cur_ch == 1) ap_beacon("OpenCafe", 1, 0, 0x10);
            if (cur_ch == 6) { ap_beacon(SSID, 6, 1, 0x01); ap_beacon("OldTKIP", 6, 2, 0x20); ap_beacon("Wpa3Only", 6, 3, 0x30); }
        } else if (type == 0 && sub == 11) { u8 body[6] = { 0, 0, 2, 0, 0, 0 }; ap_mgmt(0x00B0, ME, b, &ol, body, 6); inject(b, ol); }
        else if (type == 0 && sub == 0) {
            u8 body[6] = { 0x01, 0x04, 0, 0, 1, 0xC0 }; ap_mgmt(0x0010, ME, b, &ol, body, 6); inject(b, ol);
            if (ap_state == 1) {                                                   /* защищённая сеть: M1 */
                for (int i = 0; i < 32; i++) ap_anonce[i] = (u8)(i * 5 + 1);
                for (int i = 0; i < 8; i++) ap_replay[i] = 0; ap_replay[7] = 1;
                ap_send_eapol(0x008a, ap_anonce, 0, 0, 16);
            }
        } else if (type == 2) {                                                    /* данные клиента */
            u32 hl = 24; int prot = fc & 0x4000;
            const u8 *body = f + hl; u32 bl = n - hl; u8 plain[1600];
            if (prot) {
                u8 pnh[6] = { f[hl + 7], f[hl + 6], f[hl + 5], f[hl + 4], f[hl + 1], f[hl] }, nonce[13], aad[24];
                nonce[0] = 0; for (int i = 0; i < 6; i++) nonce[1 + i] = f[10 + i]; for (int i = 0; i < 6; i++) nonce[7 + i] = pnh[i];
                aad[0] = (u8)((fc & ~0x3800) | 0x4000); aad[1] = (u8)(((fc & ~0x3800) | 0x4000) >> 8); for (int i = 0; i < 18; i++) aad[2 + i] = f[4 + i]; aad[20] = f[22] & 0xf; aad[21] = 0;
                bl = n - hl - 16;
                if (!ccmp_open(ap_ptk + 32, nonce, aad, 22, f + hl + 8, bl, plain, f + n - 8)) { printf("AP: CCMP check failed\n"); continue; }
                body = plain;
            }
            u16 et = (u16)(body[6] << 8 | body[7]);
            if (et == 0x888e) {
                const u8 *e = body + 8; u16 info = (u16)(e[5] << 8 | e[6]);
                if (info == 0x010a) {                                              /* M2 */
                    u8 x[76]; int c = 0; for (int i = 0; i < 6; i++) if (AP[i] != ME[i]) { c = AP[i] > ME[i] ? 1 : -1; break; }
                    for (int i = 0; i < 6; i++) { x[i] = c < 0 ? AP[i] : ME[i]; x[6 + i] = c < 0 ? ME[i] : AP[i]; }
                    for (int i = 0; i < 32; i++) ap_snonce[i] = e[17 + i];
                    c = 0; for (int i = 0; i < 32; i++) if (ap_anonce[i] != ap_snonce[i]) { c = ap_anonce[i] > ap_snonce[i] ? 1 : -1; break; }
                    for (int i = 0; i < 32; i++) { x[12 + i] = c < 0 ? ap_anonce[i] : ap_snonce[i]; x[44 + i] = c < 0 ? ap_snonce[i] : ap_anonce[i]; }
                    wpa_prf(ap_pmk, 32, "Pairwise key expansion", x, 76, ap_ptk, 48);
                    u8 t[200]; u16 dl = (u16)(e[97] << 8 | e[98]); for (u32 i = 0; i < 99u + dl; i++) t[i] = e[i];
                    for (int i = 0; i < 16; i++) t[81 + i] = 0;
                    u8 h[20]; hmac_sha1(ap_ptk, 16, t, 99u + dl, h);
                    if (memcmp(h, e + 81, 16) != 0) { u8 body2[2] = { 15, 0 }; ap_mgmt(0x00C0, ME, b, &ol, body2, 2); ap_wrong = 1; inject(b, ol); continue; }
                    u8 kd[40], wr[64]; static const u8 rsn[22] = { 0x30, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0, 0 };
                    for (int i = 0; i < 22; i++) kd[i] = rsn[i];
                    /* GTK KDE: dd 16 00 0f ac 01 keyid 00 gtk[16] = 24 байта; всего 46 -> дополнить до 48 */
                    u8 big[48]; for (int i = 0; i < 22; i++) big[i] = rsn[i];
                    big[22] = 0xDD; big[23] = 22; big[24] = 0; big[25] = 0x0f; big[26] = 0xac; big[27] = 1; big[28] = 1; big[29] = 0;
                    for (int i = 0; i < 16; i++) big[30 + i] = ap_gtk[i];
                    big[46] = 0xDD; big[47] = 0;
                    ap_wrap(ap_ptk + 16, big, 6, wr);
                    ap_replay[7] = 2;
                    ap_send_eapol(0x13ca, ap_anonce, wr, 56, 16);
                } else if (info == 0x030a) { ap_ptk_ok = 1; }
            } else if (et == 0x0800) { printf("AP got IPv4 len %u\n", bl - 8); ap_pn += 0; }
        }
    }
    txn = 0;
}
static u32 T;
static void run_ticks(int n) { while (n--) { sta_tick(++T); ap_respond_all(); } }

int main(void)
{
    for (int i = 0; i < 16; i++) ap_gtk[i] = (u8)(0xA0 + i);
    sta_radio_t r = { r_tx, r_ch, r_bssid, r_link };
    sta_init(ME, &r); sta_set_callbacks(on_eth, 0);
    sta_scan(); ap_respond_all();
    CHECK(sta_scanning(), "сканирование началось");
    run_ticks(5 * 14);
    CHECK(!sta_scanning() && sta_net_count() == 4, "найдено 4 сети за 13 каналов");
    int open = -1, good = -1, tkip = -1, sae = -1;
    for (int i = 0; i < sta_net_count(); i++) { const sta_net_t *n = sta_net(i); if (!strcmp_(n->ssid, "OpenCafe")) open = i; else if (!strcmp_(n->ssid, SSID)) good = i; else if (!strcmp_(n->ssid, "OldTKIP")) tkip = i; else sae = i; }
    CHECK(good >= 0 && sta_net(good)->sec == SEC_WPA2 && sta_net(good)->channel == 6 && sta_net(good)->rssi == -50, "WPA2-PSK/CCMP распознана, канал 6");
    CHECK(open >= 0 && sta_net(open)->sec == SEC_OPEN && sta_net(open)->channel == 1, "открытая сеть распознана");
    CHECK(sta_net(tkip)->sec == SEC_UNSUPPORTED && sta_net(sae)->sec == SEC_UNSUPPORTED, "TKIP и WPA3 помечены как неподдерживаемые");
    CHECK(sta_connect(tkip, "whatever12") != 0, "к неподдерживаемой не подключается");
    CHECK(sta_connect(good, "short") == -2, "пароль короче 8 символов отклонён");
    /* открытая сеть */
    ap_state = 0; AP[5] = 0x10; CHECK(sta_connect(open, "") == 0, "подключение к открытой сети началось"); txn = txn; ap_respond_all(); run_ticks(2);
    CHECK(sta_state() == STA_CONNECTED && link_up, "открытая сеть: подключено");
    { u8 eth[60]; for (int i = 0; i < 6; i++) { eth[i] = 0xFF; eth[6 + i] = ME[i]; } eth[12] = 8; eth[13] = 0; for (int i = 14; i < 60; i++) eth[i] = (u8)i; CHECK(sta_send_eth(eth, 60) == 0, "кадр Ethernet ушёл"); }
    ap_respond_all();
    sta_disconnect(); AP[5] = 0x01; CHECK(sta_state() == STA_IDLE && !link_up, "отключение");
    /* защищённая сеть, верный пароль */
    sta_scan(); run_ticks(5 * 14);
    good = -1; for (int i = 0; i < sta_net_count(); i++) if (!strcmp_(sta_net(i)->ssid, SSID)) good = i;
    pbkdf2_sha1((const u8 *)PASS, 13, (const u8 *)SSID, 7, 4096, ap_pmk, 32);
    ap_state = 1; ap_wrong = 0; ap_ptk_ok = 0;
    CHECK(sta_connect(good, PASS) == 0, "WPA2: подключение началось");
    ap_respond_all(); run_ticks(3);
    CHECK(sta_state() == STA_CONNECTED && link_up && ap_ptk_ok, "четырёхэтапное рукопожатие прошло, M4 принят точкой");
    /* данные от точки: одноадресные и широковещательные */
    { u8 pay[64]; for (int i = 0; i < 64; i++) pay[i] = (u8)(i ^ 0x5a); u8 f[600]; u32 n = ap_data_out(f, ME, AP, 0x0800, pay, 64, 1, ap_ptk + 32, 0); eth_n = 0; inject(f, n);
      CHECK(eth_n == 1 && eth_len[0] == 78 && eth_got[0][12] == 8 && eth_got[0][13] == 0 && !memcmp(eth_got[0] + 14, pay, 64) && !memcmp(eth_got[0] + 6, AP, 6) && !memcmp(eth_got[0], ME, 6), "одноадресный кадр расшифрован и превращён в Ethernet");
      u8 bc[6] = { 255, 255, 255, 255, 255, 255 }; static const u8 src[6] = { 0x02, 9, 9, 9, 9, 9 };
      n = ap_data_out(f, bc, src, 0x0806, pay, 28, 1, ap_gtk, 1); eth_n = 0; inject(f, n);
      CHECK(eth_n == 1 && eth_len[0] == 42 && eth_got[0][12] == 8 && eth_got[0][13] == 6 && !memcmp(eth_got[0] + 6, src, 6), "широковещательный кадр расшифрован ключом группы (GTK из M3)");
      f[40] ^= 1; eth_n = 0; inject(f, n); CHECK(eth_n == 0, "кадр с испорченным MIC отброшен"); }
    { u8 eth[100]; for (int i = 0; i < 6; i++) { eth[i] = AP[i]; eth[6 + i] = ME[i]; } eth[12] = 8; eth[13] = 0; for (int i = 14; i < 100; i++) eth[i] = (u8)(i * 3); txn = 0; CHECK(sta_send_eth(eth, 100) == 0, "кадр клиента зашифрован"); ap_respond_all(); }
    sta_disconnect();
    /* неверный пароль */
    sta_scan(); run_ticks(5 * 14);
    good = -1; for (int i = 0; i < sta_net_count(); i++) if (!strcmp_(sta_net(i)->ssid, SSID)) good = i;
    ap_wrong = 0; ap_ptk_ok = 0;
    CHECK(sta_connect(good, "wrong password") == 0, "неверный пароль: попытка");
    ap_respond_all(); run_ticks(3);
    CHECK(ap_wrong && sta_state() == STA_FAILED && !strcmp_(sta_text(), "Incorrect password."), "неверный пароль: точка отклонила MIC, пользователь видит причину");
    /* тишина */
    sta_scan(); run_ticks(5 * 14); good = -1; for (int i = 0; i < sta_net_count(); i++) if (!strcmp_(sta_net(i)->ssid, SSID)) good = i;
    sta_connect(good, PASS); txn = 0; for (int i = 0; i < 400; i++) { sta_tick(++T); txn = 0; }
    CHECK(sta_state() == STA_FAILED, "точка молчит: ошибка по таймауту");
    return fails;
}
