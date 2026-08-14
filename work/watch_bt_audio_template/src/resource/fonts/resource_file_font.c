#include "lvgl/lvgl.h"

#ifdef LV_USING_FREETYPE_ENGINE

#include "app_module.h"
#include "lvsf_ft_reg.h"

/* tiny55_full_lib: external FreeType library descriptor.
 * The file is provisioned by fs_root.bin and updated independently from the
 * executable image, so routine firmware OTA packages do not carry the font.
 */
const lv_font_freetype_lib_dsc_t tiny55_full_lib =
{
    .font_lib_size = 0U,
    .font_lib_data = "/ex/font/tiny55_full.ttf",
    .font_lib_name = "tiny55_full",
};

LVSF_FREETYPE_FONT_REGISTER(tiny55_full);

#endif /* LV_USING_FREETYPE_ENGINE */
