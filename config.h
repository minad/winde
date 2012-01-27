#ifndef OUT_ALIAS
#  define OUT_ALIAS(name, port, bit, alias) OUT(name, port, bit)
#endif
#ifndef OUT
#  define OUT(name, port, bit)
#endif
#ifndef IN_ALIAS
#  define IN_ALIAS(name, port, bit, alias) IN(name, port, bit)
#endif
#ifndef IN
#  define IN(name, port, bit)
#endif
#ifndef STATE
#  define STATE(name, action)
#endif
#ifndef ACTION
#  define ACTION(name, code)
#endif
#ifndef EVENT
#  define EVENT(name, condition)
#endif
#ifndef TRANSITION
#  define TRANSITION(initial, event, final)
#endif

#include "config"

#undef OUT
#undef IN
#undef OUT_ALIAS
#undef IN_ALIAS
#undef STATE
#undef ACTION
#undef EVENT
#undef TRANSITION
