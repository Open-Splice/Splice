#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"

void recurse(int a) {
    printf("%d\n", a);
    recurse(a + 1);
}
int main() {
    recurse(1);
    return 0;
}