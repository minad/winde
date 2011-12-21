#define VA_NUM_ARGS(...)                    VA_NUM_ARGS_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define VA_NUM_ARGS_(_1,_2,_3,_4,_5,N, ...) N

#define CONCAT(a, b)                        a ## b
#define DISPATCH(name, ...)                 DISPATCH_(name, VA_NUM_ARGS(__VA_ARGS__))
#define DISPATCH_(name, nargs)              CONCAT(name, nargs)
#define OUT(...)                            DISPATCH(OUT, __VA_ARGS__)(__VA_ARGS__)
#define IN(...)                             DISPATCH(IN, __VA_ARGS__)(__VA_ARGS__)

#ifndef OUT4
#  define OUT4(name, port, bit, alias) OUT3(name, port, bit)
#endif
#ifndef OUT3
#  define OUT3(name, port, bit)
#endif
#ifndef IN4
#  define IN4(name, port, bit, alias) IN3(name, port, bit)
#endif
#ifndef IN3
#  define IN3(name, port, bit)
#endif

#include "config.h"

#undef VA_NUM_ARGS
#undef CONCAT
#undef DISPATCH
#undef OUT
#undef IN
#undef OUT4
#undef OUT3
#undef IN4
#undef IN3
