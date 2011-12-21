#define HAS_ALIAS(...)            HAS_ALIAS_(__VA_ARGS__, _WITH_ALIAS,)
#define HAS_ALIAS_(a, b, ...)     b
#define CONCAT(a, b)              CONCAT_(a, b)
#define CONCAT_(a, b)             a ## b
#define OUT(name, port, bit, ...) CONCAT(OUTPUT, HAS_ALIAS(__VA_ARGS__))(name, port, bit, __VA_ARGS__)
#define IN(name, port, bit, ...)  CONCAT(INPUT, HAS_ALIAS(__VA_ARGS__))(name, port, bit, __VA_ARGS__)

#ifndef OUTPUT_WITH_ALIAS
#  define OUTPUT_WITH_ALIAS(name, port, bit, alias) OUTPUT(name, port, bit)
#endif
#ifndef OUTPUT
#  define OUTPUT(name, port, bit)
#endif
#ifndef INPUT_WITH_ALIAS
#  define INPUT_WITH_ALIAS(name, port, bit, alias) INPUT(name, port, bit)
#endif
#ifndef INPUT
#  define INPUT(name, port, bit)
#endif

#include "config.h"

#undef HAS_ALIAS
#undef HAS_ALIAS_
#undef CONCAT
#undef CONCAT_
#undef OUT
#undef IN
#undef OUTPUT_WITH_ALIAS
#undef OUTPUT
#undef INPUT_WITH_ALIAS
#undef INPUT
