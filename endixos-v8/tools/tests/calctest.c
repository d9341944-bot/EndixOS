extern int printf(const char *, ...);
#include "gui/calc.c"
static int fails;
static void run(const char *seq, const char *want)
{
    calc_reset();
    for (const char *p = seq; *p; p++) calc_key(*p);
    char b[40];
    if (err) { b[0]='E';b[1]='r';b[2]='r';b[3]='o';b[4]='r';b[5]=0; }
    else if (has_entry) { int n = 0; for (int i = 0; i < en; i++) b[n++] = entry[i]; b[n] = 0; }
    else format(acc, b);
    int ok = 1, i = 0; while (want[i] || b[i]) { if (want[i] != b[i]) { ok = 0; break; } i++; }
    printf("%s  %s = %s%s%s\n", ok ? "PASS" : "FAIL", seq, b, ok ? "" : "  want ", ok ? "" : want);
    if (!ok) fails++;
}
int main(void)
{
    run("12.5*4=", "50"); run("1/3=", "0.3333"); run("2*3.5=", "7"); run("5/0=", "Error");
    run("0.1+0.2=", "0.3"); run("999999999*9=", "Error"); run("7-9=", "-2"); run("100/8=", "12.5");
    run("2+3*4=", "20"); run("9n+4=", "-5"); run("50%", "0.5"); run("1.23456+0=", "1.2345");
    run("8*8*8=", "512"); run("6/4=", "1.5"); run("0.5*0.5=", "0.25"); run("123456789+1=", "123456790");
    run("12\b3", "13");
    printf("fails=%d\n", fails); return fails;
}
