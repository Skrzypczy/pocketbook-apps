/* FreeType in PocketBook SDK is older than 2.10 and lacks FT_Error_String. */
#include <stddef.h>

const char *FT_Error_String(int error_code)
{
    (void)error_code;
    return NULL;
}
