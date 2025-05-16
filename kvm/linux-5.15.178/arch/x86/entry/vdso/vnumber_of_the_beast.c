// #include <asm/linkage.h>

// notrace int __vdso_number_of_the_beast(void)
// {
//     return 0xDEAD - 56339;
// }

// int number_of_the_beast(void)
//     __attribute__((weak, alias("__vdso_number_of_the_beast")));