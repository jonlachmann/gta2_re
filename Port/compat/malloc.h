// Port (docs/port.md): macOS has no <malloc.h>; malloc and free are in <stdlib.h>.
#pragma once
#if defined(__APPLE__)
    #include <stdlib.h>
#else
    #include_next <malloc.h>
#endif
