#include "parse_port.h"
#include <cstdio>
#include <cstdlib>

static void Expect(bool cond, const char *msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    int v = -1;
    Expect(ParsePort("1", v) && v == 1, "one");
    Expect(ParsePort("10178", v) && v == 10178, "hdc");
    Expect(ParsePort("65535", v) && v == 65535, "maxport");
    Expect(ParsePort("0", v) && v == 0, "zero");
    Expect(!ParsePort("", v), "empty");
    Expect(!ParsePort("abc", v), "abc");
    Expect(!ParsePort("80a", v), "80a");
    Expect(!ParsePort("9999999999999999999", v), "huge");
    Expect(!ParsePort("2147483648", v), "overflow");
    Expect(!ParsePort(" 80", v), "space");
    std::puts("ok");
    return 0;
}
