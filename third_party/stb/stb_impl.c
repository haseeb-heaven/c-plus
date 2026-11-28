/* Single external translation unit for the vendored stb image decoders.
 * The library is compiled once here, exactly as upstream documents; nothing is
 * pasted into a .hp header.  See third_party/stb/LICENSE (public domain / MIT).
 */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"
#include "stb_image_write.h"
