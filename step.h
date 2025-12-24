#pragma once
#define N(argo) void argo(int*o, int b, int a, int t, int s, int d)
#define S(argo) static N(argo)
#define C o, b, a, t, s, d
#define Step(a, b, n, s, d) N(a##b##_##n) { t = o[b s n s d], b = o[t s 4 s d], (o[t s 5 s d] + cm)(C); }
N(cm);
#define Q0(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q1(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q01(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q2(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q02(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q12(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q012(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = o[b s 3 s d], \
    o[a s 4 s d] = b, \
    b = a
#define Q3(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q03(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q13(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q013(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = o[b s 2 s d], \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q23(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q023(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = o[b s 1 s d], \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q123(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = o[b s 0 s d], \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define Q0123(a, b, s, d) \
    a = a s - 5, \
    o[a s 0 s d] = a, \
    o[a s 1 s d] = a, \
    o[a s 2 s d] = a, \
    o[a s 3 s d] = a, \
    o[a s 4 s d] = b, \
    b = a
#define AB_Red(step) o[a++] = step - cm, Q0(a, b, -, 1)
#define AB_Yellow(step) o[a++] = step - cm, Q1(a, b, -, 1)
#define AB_Red_Yellow(step) o[a++] = step - cm, Q01(a, b, -, 1)
#define AB_Green(step) o[a++] = step - cm, Q2(a, b, -, 1)
#define AB_Red_Green(step) o[a++] = step - cm, Q02(a, b, -, 1)
#define AB_Yellow_Green(step) o[a++] = step - cm, Q12(a, b, -, 1)
#define AB_Red_Yellow_Green(step) o[a++] = step - cm, Q012(a, b, -, 1)
#define AB_Blue(step) o[a++] = step - cm, Q3(a, b, -, 1)
#define AB_Red_Blue(step) o[a++] = step - cm, Q03(a, b, -, 1)
#define AB_Yellow_Blue(step) o[a++] = step - cm, Q13(a, b, -, 1)
#define AB_Red_Yellow_Blue(step) o[a++] = step - cm, Q013(a, b, -, 1)
#define AB_Green_Blue(step) o[a++] = step - cm, Q23(a, b, -, 1)
#define AB_Red_Green_Blue(step) o[a++] = step - cm, Q023(a, b, -, 1)
#define AB_Yellow_Green_Blue(step) o[a++] = step - cm, Q123(a, b, -, 1)
#define AB_Red_Yellow_Green_Blue(step) o[a++] = step - cm, Q0123(a, b, -, 1)
#define SD_Red(step) o[--s] = step - cm, Q0(s, d, +, 0)
#define SD_Yellow(step) o[--s] = step - cm, Q1(s, d, +, 0)
#define SD_Red_Yellow(step) o[--s] = step - cm, Q01(s, d, +, 0)
#define SD_Green(step) o[--s] = step - cm, Q2(s, d, +, 0)
#define SD_Red_Green(step) o[--s] = step - cm, Q02(s, d, +, 0)
#define SD_Yellow_Green(step) o[--s] = step - cm, Q12(s, d, +, 0)
#define SD_Red_Yellow_Green(step) o[--s] = step - cm, Q012(s, d, +, 0)
#define SD_Blue(step) o[--s] = step - cm, Q3(s, d, +, 0)
#define SD_Red_Blue(step) o[--s] = step - cm, Q03(s, d, +, 0)
#define SD_Yellow_Blue(step) o[--s] = step - cm, Q13(s, d, +, 0)
#define SD_Red_Yellow_Blue(step) o[--s] = step - cm, Q013(s, d, +, 0)
#define SD_Green_Blue(step) o[--s] = step - cm, Q23(s, d, +, 0)
#define SD_Red_Green_Blue(step) o[--s] = step - cm, Q023(s, d, +, 0)
#define SD_Yellow_Green_Blue(step) o[--s] = step - cm, Q123(s, d, +, 0)
#define SD_Red_Yellow_Green_Blue(step) o[--s] = step - cm, Q0123(s, d, +, 0)
