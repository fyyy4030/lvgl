/**
 * @file lv_draw_rect.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_rect.h"
#include "lv_draw_blend.h"
#include "lv_draw_mask.h"
#include "../lv_misc/lv_circ.h"
#include "../lv_misc/lv_math.h"
#include "../lv_core/lv_refr.h"

/*********************
 *      DEFINES
 *********************/
#define SHADOW_UPSACALE_SHIFT   6
#define SHADOW_ENHANCE          1

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void draw_bg(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc);
static void draw_border(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc);
static void draw_shadow(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc);
static lv_color_t grad_get(lv_draw_rect_dsc_t * dsc, lv_coord_t s, lv_coord_t i);
static void shadow_draw_corner_buf(const lv_area_t * coords,  lv_opa_t * sh_buf, lv_coord_t s, lv_coord_t r);
static void shadow_blur_corner(lv_coord_t size, lv_coord_t sw, lv_opa_t * res_buf, uint16_t * sh_ups_buf);
static void draw_img(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t * dsc)
{
    memset(dsc, 0x00, sizeof(lv_draw_rect_dsc_t));
    dsc->bg_grad_color_stop = 0xFF;
    dsc->bg_opa = LV_OPA_COVER;
    dsc->border_opa = LV_OPA_COVER;
    dsc->overlay_opa = LV_OPA_TRANSP;
    dsc->pattern_font = LV_FONT_DEFAULT;
    dsc->shadow_opa = LV_OPA_COVER;
    dsc->border_part = LV_BORDER_SIDE_FULL;
}

/**
 * Draw a rectangle
 * @param coords the coordinates of the rectangle
 * @param mask the rectangle will be drawn only in this mask
 * @param style pointer to a style
 */
void lv_draw_rect(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc)
{
    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) return;

    draw_shadow(coords, clip, dsc);
    draw_bg(coords, clip, dsc);
    draw_img(coords, clip, dsc);
    draw_border(coords, clip, dsc);
}

/**
 * Draw a pixel
 * @param point the coordinates of the point to draw
 * @param mask the pixel will be drawn only in this mask
 * @param style pointer to a style
 * @param opa_scale scale down the opacity by the factor
 */
void lv_draw_px(const lv_point_t * point, const lv_area_t * clip_area, const lv_style_t * style)
{
//    lv_opa_t opa = style->body.opa;
//    if(opa_scale != LV_OPA_COVER) opa = (opa * opa_scale) >> 8;
//
//    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;
//
//    lv_area_t fill_area;
//    fill_area.x1 = point->x;
//    fill_area.y1 = point->y;
//    fill_area.x2 = point->x;
//    fill_area.y2 = point->y;
//
//    uint8_t mask_cnt = lv_draw_mask_get_cnt();
//
//    if(mask_cnt == 0) {
//        lv_blend_fill(clip_area, &fill_area, style->body.main_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, style->body.blend_mode);
//    } else {
//        uint8_t mask_buf;
//        lv_draw_mask_res_t mask_res;
//        mask_res = lv_draw_mask_apply(&mask_buf, point->x, point->y, 1);
//        lv_blend_fill(clip_area, &fill_area, style->body.main_color, &mask_buf, mask_res, opa, style->body.blend_mode);
//    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void draw_bg(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc)
{
    lv_area_t coords_bg;
    lv_area_copy(&coords_bg, coords);

    /*If the border fully covers make the bg area 1px smaller to avoid artifacts on the corners*/
    if(dsc->border_width > 1 && dsc->border_opa >= LV_OPA_MAX && dsc->radius != 0) {
        coords_bg.x1++;
        coords_bg.y1++;
        coords_bg.x2--;
        coords_bg.y2--;
    }

    lv_opa_t opa = dsc->bg_opa;

    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

    lv_disp_t * disp    = lv_refr_get_disp_refreshing();
    lv_disp_buf_t * vdb = lv_disp_get_buf(disp);

    /* Get clipped fill area which is the real draw area.
     * It is always the same or inside `fill_area` */
    lv_area_t draw_area;
    bool is_common;
    is_common = lv_area_intersect(&draw_area, &coords_bg, clip);
    if(is_common == false) return;

    const lv_area_t * disp_area = &vdb->area;

    /* Now `draw_area` has absolute coordinates.
     * Make it relative to `disp_area` to simplify draw to `disp_buf`*/
    draw_area.x1 -= disp_area->x1;
    draw_area.y1 -= disp_area->y1;
    draw_area.x2 -= disp_area->x1;
    draw_area.y2 -= disp_area->y1;

    lv_coord_t draw_area_w = lv_area_get_width(&draw_area);

    /*Create a mask if there is a radius*/
    lv_opa_t * mask_buf = lv_mem_buf_get(draw_area_w);

    bool simple_mode = true;
    if(lv_draw_mask_get_cnt()!= 0) simple_mode = false;
    else if(dsc->bg_grad_dir == LV_GRAD_DIR_HOR) simple_mode = false;

    int16_t mask_rout_id = LV_MASK_ID_INV;

    lv_coord_t coords_w = lv_area_get_width(&coords_bg);
    lv_coord_t coords_h = lv_area_get_height(&coords_bg);

    /*Get the real radius*/
    lv_coord_t rout = dsc->radius;
    lv_coord_t short_side = LV_MATH_MIN(coords_w, coords_h);
    if(rout > short_side >> 1) rout = short_side >> 1;

    /*Most simple case: just a plain rectangle*/
    if(simple_mode && rout == 0 && (dsc->bg_grad_dir == LV_GRAD_DIR_NONE)) {
        lv_blend_fill(clip, &coords_bg,
                dsc->bg_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa,
                dsc->bg_blend_mode);
    }
    /*More complex case: there is a radius, gradient or mask.*/
    else {
        lv_draw_mask_radius_param_t mask_rout_param;
        if(rout > 0) {
            lv_draw_mask_radius_init(&mask_rout_param, &coords_bg, rout, false);
            mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);
        }

        if(opa >= LV_OPA_MIN) {
            /*Draw the background line by line*/
            lv_coord_t h;
            lv_draw_mask_res_t mask_res = LV_DRAW_MASK_RES_FULL_COVER;
            lv_color_t grad_color = dsc->bg_color;


            lv_color_t * grad_map = NULL;
            /*In case of horizontal gradient pre-compute a line with a gradient*/
            if(dsc->bg_grad_dir == LV_GRAD_DIR_HOR && dsc->bg_color.full != dsc->bg_grad_color.full) {
                grad_map = lv_mem_buf_get(coords_w * sizeof(lv_color_t));

                lv_coord_t i;
                for(i = 0; i < coords_w; i++) {
                    grad_map[i] = grad_get(dsc, coords_w, i);
                }
            }

            lv_area_t fill_area;
            fill_area.x1 = coords_bg.x1;
            fill_area.x2 = coords_bg.x2;
            fill_area.y1 = disp_area->y1 + draw_area.y1;
            fill_area.y2 = fill_area.y1;
            for(h = draw_area.y1; h <= draw_area.y2; h++) {
                lv_coord_t y = h + vdb->area.y1;

                /*In not corner areas apply the mask only if required*/
                if(y > coords_bg.y1 + rout + 1 &&
                        y < coords_bg.y2 - rout - 1) {
                    mask_res = LV_DRAW_MASK_RES_FULL_COVER;
                    if(simple_mode == false) {
                        memset(mask_buf, LV_OPA_COVER, draw_area_w);
                        mask_res = lv_draw_mask_apply(mask_buf, vdb->area.x1 + draw_area.x1, vdb->area.y1 + h, draw_area_w);
                    }
                }
                /*In corner areas apply the mask anyway*/
                else {
                    memset(mask_buf, LV_OPA_COVER, draw_area_w);
                    mask_res = lv_draw_mask_apply(mask_buf, vdb->area.x1 + draw_area.x1, vdb->area.y1 + h, draw_area_w);
                }

                /*Get the current line color*/
                if(dsc->bg_grad_dir == LV_GRAD_DIR_VER && dsc->bg_color.full != dsc->bg_grad_color.full) {
                    grad_color = grad_get(dsc, lv_area_get_height(&coords_bg), y - coords_bg.y1);
                }

                /* If there is not other mask and drawing the corner area split the drawing to corner and middle areas
                 * because it the middle mask shuldn't be taken into account (therefore its faster)*/
                if(simple_mode &&
                        (y < coords_bg.y1 + rout + 1 ||
                                y > coords_bg.y2 - rout - 1)) {

                    /*Left part*/
                    lv_area_t fill_area2;
                    fill_area2.x1 = coords_bg.x1;
                    fill_area2.x2 = coords_bg.x1 + rout - 1;
                    fill_area2.y1 = fill_area.y1;
                    fill_area2.y2 = fill_area.y2;

                    lv_blend_fill(clip, &fill_area2,
                            grad_color, mask_buf, mask_res, opa, dsc->bg_blend_mode);


                    /*Central part*/
                    fill_area2.x1 = coords_bg.x1 + rout;
                    fill_area2.x2 = coords_bg.x2 - rout;

                    lv_blend_fill(clip, &fill_area2,
                            grad_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, dsc->bg_blend_mode);

                    fill_area2.x1 = coords_bg.x2 - rout + 1;
                    fill_area2.x2 = coords_bg.x2;

                    lv_coord_t mask_ofs = (coords_bg.x2 - rout + 1) - (vdb->area.x1 + draw_area.x1);
                    if(mask_ofs < 0) mask_ofs = 0;
                    lv_blend_fill(clip, &fill_area2,
                            grad_color, mask_buf + mask_ofs, mask_res, opa, dsc->bg_blend_mode);
                } else {
                    if(grad_map == NULL) {
                        lv_blend_fill(clip, &fill_area,
                                grad_color,mask_buf, mask_res, opa, dsc->bg_blend_mode);
                    } else {
                        lv_blend_map(clip, &fill_area, grad_map, mask_buf, mask_res, opa, dsc->bg_blend_mode);
                    }

                }
                fill_area.y1++;
                fill_area.y2++;
            }

            if(grad_map) lv_mem_buf_release(grad_map);
        }

        lv_draw_mask_remove_id(mask_rout_id);
    }

    lv_mem_buf_release(mask_buf);

}

static void draw_border(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc)
{
    lv_coord_t border_width = dsc->border_width;
    if(border_width == 0) return;

    lv_opa_t opa = dsc->border_opa;

    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

    lv_disp_t * disp    = lv_refr_get_disp_refreshing();
    lv_disp_buf_t * vdb = lv_disp_get_buf(disp);

    /* Get clipped fill area which is the real draw area.
     * It is always the same or inside `fill_area` */
    lv_area_t draw_area;
    bool is_common;
    is_common = lv_area_intersect(&draw_area, coords, clip);
    if(is_common == false) return;

    const lv_area_t * disp_area = &vdb->area;

    /* Now `draw_area` has absolute coordinates.
     * Make it relative to `disp_area` to simplify draw to `disp_buf`*/
    draw_area.x1 -= disp_area->x1;
    draw_area.y1 -= disp_area->y1;
    draw_area.x2 -= disp_area->x1;
    draw_area.y2 -= disp_area->y1;

    lv_coord_t draw_area_w = lv_area_get_width(&draw_area);

    /*Create a mask if there is a radius*/
    lv_opa_t * mask_buf = lv_mem_buf_get(draw_area_w);

    bool simple_mode = true;
    if(lv_draw_mask_get_cnt()!= 0) simple_mode = false;
    else if(dsc->border_part != LV_BORDER_SIDE_FULL) simple_mode = false;
    else if(dsc->bg_grad_dir == LV_GRAD_DIR_HOR) simple_mode = false;

    int16_t mask_rout_id = LV_MASK_ID_INV;

    lv_coord_t coords_w = lv_area_get_width(coords);
    lv_coord_t coords_h = lv_area_get_height(coords);

    /*Get the real radius*/
    lv_coord_t rout = dsc->radius;
    lv_coord_t short_side = LV_MATH_MIN(coords_w, coords_h);
    if(rout > short_side >> 1) rout = short_side >> 1;

    /*Get the outer area*/
    lv_draw_mask_radius_param_t mask_rout_param;
    if(rout > 0) {
        lv_draw_mask_radius_init(&mask_rout_param, coords, rout, false);
        mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);
    }


    /*Get the inner radius*/
    lv_coord_t rin = rout - border_width;
    if(rin < 0) rin = 0;

    /*Get the inner area*/
    lv_area_t area_small;
    lv_area_copy(&area_small, coords);
    area_small.x1 += ((dsc->border_part & LV_BORDER_SIDE_LEFT) ? border_width : - (border_width + rout));
    area_small.x2 -= ((dsc->border_part & LV_BORDER_SIDE_RIGHT) ? border_width : - (border_width + rout));
    area_small.y1 += ((dsc->border_part & LV_BORDER_SIDE_TOP) ? border_width : - (border_width + rout));
    area_small.y2 -= ((dsc->border_part & LV_BORDER_SIDE_BOTTOM) ? border_width : - (border_width + rout));

    /*Create inner the mask*/
    lv_draw_mask_radius_param_t mask_rin_param;
    lv_draw_mask_radius_init(&mask_rin_param, &area_small, rout - border_width, true);
    int16_t mask_rin_id = lv_draw_mask_add(&mask_rin_param, NULL);

    lv_coord_t corner_size = LV_MATH_MAX(rout, border_width - 1);

    lv_coord_t h;
    lv_draw_mask_res_t mask_res;
    lv_area_t fill_area;

    lv_color_t color = dsc->border_color;
    lv_blend_mode_t blend_mode = dsc->border_blend_mode;

    /*Apply some optimization if there is no other mask*/
    if(simple_mode) {
        /*Draw the upper corner area*/
        lv_coord_t upper_corner_end = coords->y1 - disp_area->y1 + corner_size;

        fill_area.x1 = coords->x1;
        fill_area.x2 = coords->x2;
        fill_area.y1 = disp_area->y1 + draw_area.y1;
        fill_area.y2 = fill_area.y1;
        for(h = draw_area.y1; h <= upper_corner_end; h++) {
            memset(mask_buf, LV_OPA_COVER, draw_area_w);
            mask_res = lv_draw_mask_apply(mask_buf, vdb->area.x1 + draw_area.x1, vdb->area.y1 + h, draw_area_w);

            lv_area_t fill_area2;
            fill_area2.y1 = fill_area.y1;
            fill_area2.y2 = fill_area.y2;

            fill_area2.x1 = coords->x1;
            fill_area2.x2 = coords->x1 + rout - 1;

            lv_blend_fill(clip, &fill_area2, color, mask_buf, mask_res, opa, blend_mode);

            if(fill_area2.y2 < coords->y1 + border_width) {
                fill_area2.x1 = coords->x1 + rout;
                fill_area2.x2 = coords->x2 - rout;

                lv_blend_fill(clip, &fill_area2, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
            }

            fill_area2.x1 = coords->x2 - rout + 1;
            fill_area2.x2 = coords->x2;

            lv_coord_t mask_ofs = (coords->x2 - rout + 1) - (vdb->area.x1 + draw_area.x1);
            if(mask_ofs < 0) mask_ofs = 0;
            lv_blend_fill(clip, &fill_area2, color, mask_buf + mask_ofs, mask_res, opa, blend_mode);

            fill_area.y1++;
            fill_area.y2++;
        }

        /*Draw the lower corner area corner area*/
        if(dsc->border_part & LV_BORDER_SIDE_BOTTOM) {
            lv_coord_t lower_corner_end = coords->y2 - disp_area->y1 - corner_size;
            if(lower_corner_end <= upper_corner_end) lower_corner_end = upper_corner_end + 1;
            fill_area.y1 = disp_area->y1 + lower_corner_end;
            fill_area.y2 = fill_area.y1;
            for(h = lower_corner_end; h <= draw_area.y2; h++) {
                memset(mask_buf, LV_OPA_COVER, draw_area_w);
                mask_res = lv_draw_mask_apply(mask_buf, vdb->area.x1 + draw_area.x1, vdb->area.y1 + h, draw_area_w);

                lv_area_t fill_area2;
                fill_area2.x1 = coords->x1;
                fill_area2.x2 = coords->x1 + rout - 1;
                fill_area2.y1 = fill_area.y1;
                fill_area2.y2 = fill_area.y2;

                lv_blend_fill(clip, &fill_area2, color, mask_buf, mask_res, opa, blend_mode);


                if(fill_area2.y2 > coords->y2 - border_width ) {
                    fill_area2.x1 = coords->x1 + rout;
                    fill_area2.x2 = coords->x2 - rout;

                    lv_blend_fill(clip, &fill_area2, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
                }
                fill_area2.x1 = coords->x2 - rout + 1;
                fill_area2.x2 = coords->x2;

                lv_coord_t mask_ofs = (coords->x2 - rout + 1) - (vdb->area.x1 + draw_area.x1);
                if(mask_ofs < 0) mask_ofs = 0;
                lv_blend_fill(clip, &fill_area2, color, mask_buf + mask_ofs, mask_res, opa, blend_mode);


                fill_area.y1++;
                fill_area.y2++;
            }
        }

        /*Draw the left vertical border part*/
        fill_area.y1 = coords->y1 + corner_size + 1;
        fill_area.y2 = coords->y2 - corner_size - 1;

        fill_area.x1 = coords->x1;
        fill_area.x2 = coords->x1 + border_width - 1;
        lv_blend_fill(clip, &fill_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);

        /*Draw the right vertical border*/
        fill_area.x1 = coords->x2 - border_width + 1;
        fill_area.x2 = coords->x2;

        lv_blend_fill(clip, &fill_area, color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa, blend_mode);
    }
    /*Process line by line if there is other mask too*/
    else {
        fill_area.x1 = coords->x1;
        fill_area.x2 = coords->x2;
        fill_area.y1 = disp_area->y1 + draw_area.y1;
        fill_area.y2 = fill_area.y1;
        for(h = draw_area.y1; h <= draw_area.y2; h++) {
            memset(mask_buf, LV_OPA_COVER, draw_area_w);
            mask_res = lv_draw_mask_apply(mask_buf, vdb->area.x1 + draw_area.x1, vdb->area.y1 + h, draw_area_w);

            lv_blend_fill( clip, &fill_area, color, mask_buf, mask_res, opa, blend_mode);

            fill_area.y1++;
            fill_area.y2++;

        }
    }
    lv_draw_mask_remove_id(mask_rin_id);
    lv_draw_mask_remove_id(mask_rout_id);
    lv_mem_buf_release(mask_buf);
}

static lv_color_t grad_get(lv_draw_rect_dsc_t * dsc, lv_coord_t s, lv_coord_t i)
{
    lv_coord_t min = (dsc->bg_main_color_stop * s) >> 8;
    if(i <= min) return dsc->bg_color;

    lv_coord_t max = (dsc->bg_grad_color_stop * s) >> 8;
    if(i >= max) return dsc->bg_grad_color;

    lv_coord_t d = dsc->bg_grad_color_stop - dsc->bg_main_color_stop;
    d = (s * d) >> 8;
    i -= min;
    lv_opa_t mix = (i * 255) / d;
    return lv_color_mix(dsc->bg_grad_color, dsc->bg_color, mix);
}

static void draw_shadow(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc)
{
    /*Check whether the shadow is visible*/
    if(dsc->shadow_width == 0) return;

    if(dsc->shadow_width == 1 && dsc->shadow_ofs_x == 0 &&
    		dsc->shadow_ofs_y == 0 && dsc->shadow_spread <= 0) {
        return;
    }

    lv_coord_t sw = dsc->shadow_width;

    lv_area_t sh_rect_area;
    sh_rect_area.x1 = coords->x1  + dsc->shadow_ofs_x - dsc->shadow_spread;
    sh_rect_area.x2 = coords->x2  + dsc->shadow_ofs_x + dsc->shadow_spread;
    sh_rect_area.y1 = coords->y1  + dsc->shadow_ofs_y - dsc->shadow_spread;
    sh_rect_area.y2 = coords->y2  + dsc->shadow_ofs_y + dsc->shadow_spread;

    lv_area_t sh_area;
    sh_area.x1 = sh_rect_area.x1 - sw / 2 - 1;
    sh_area.x2 = sh_rect_area.x2 + sw / 2 + 1;
    sh_area.y1 = sh_rect_area.y1 - sw / 2 - 1;
    sh_area.y2 = sh_rect_area.y2 + sw / 2 + 1;

    lv_opa_t opa = dsc->shadow_opa;

    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

    lv_disp_t * disp    = lv_refr_get_disp_refreshing();
    lv_disp_buf_t * vdb = lv_disp_get_buf(disp);

    /* Get clipped fill area which is the real draw area.
     * It is always the same or inside `fill_area` */
    lv_area_t draw_area;
    bool is_common;
    is_common = lv_area_intersect(&draw_area, &sh_area, clip);
    if(is_common == false) return;

    const lv_area_t * disp_area = &vdb->area;

    /* Now `draw_area` has absolute coordinates.
     * Make it relative to `disp_area` to simplify draw to `disp_buf`*/
    draw_area.x1 -= disp_area->x1;
    draw_area.y1 -= disp_area->y1;
    draw_area.x2 -= disp_area->x1;
    draw_area.y2 -= disp_area->y1;

    /*Consider 1 px smaller bg to be sure the edge will be covered by the shadow*/
    lv_area_t bg_coords;
    lv_area_copy(&bg_coords, coords);
    bg_coords.x1 += 1;
    bg_coords.y1 += 1;
    bg_coords.x2 -= 1;
    bg_coords.y2 -= 1;

    /*Get the real radius*/
    lv_coord_t r_bg = dsc->radius;
    lv_coord_t short_side = LV_MATH_MIN(lv_area_get_width(&bg_coords), lv_area_get_height(&bg_coords));
    if(r_bg > short_side >> 1) r_bg = short_side >> 1;

    lv_coord_t r_sh = dsc->radius;
    short_side = LV_MATH_MIN(lv_area_get_width(&sh_rect_area), lv_area_get_height(&sh_rect_area));
    if(r_sh > short_side >> 1) r_sh = short_side >> 1;


    lv_coord_t corner_size = sw  + r_sh;

    lv_opa_t * sh_buf = lv_mem_buf_get(corner_size * corner_size);
    shadow_draw_corner_buf(&sh_rect_area, sh_buf, dsc->shadow_width, r_sh);

    bool simple_mode = true;
    if(lv_draw_mask_get_cnt() > 0) simple_mode = false;
    else if(dsc->shadow_ofs_x != 0 || dsc->shadow_ofs_y != 0) simple_mode = false;
    else if(dsc->shadow_spread != 0) simple_mode = false;

    lv_coord_t y_max;

    /*Create a mask*/
    lv_draw_mask_res_t mask_res;
    lv_opa_t * mask_buf = lv_mem_buf_get(lv_area_get_width(&sh_rect_area));

    lv_draw_mask_radius_param_t mask_rout_param;
    lv_draw_mask_radius_init(&mask_rout_param, &bg_coords, r_bg, true);

    int16_t mask_rout_id = LV_MASK_ID_INV;
    mask_rout_id = lv_draw_mask_add(&mask_rout_param, NULL);

    lv_area_t a;

    /*Draw the top right corner*/
    a.x2 = sh_area.x2;
    a.x1 = a.x2 - corner_size + 1;
    a.y1 = sh_area.y1;
    a.y2 = a.y1;

    lv_coord_t first_px;
    first_px = 0;
    if(disp_area->x1 > a.x1) {
        first_px = disp_area->x1 - a.x1;
    }

    lv_coord_t hor_mid_dist = (sh_area.x1 + lv_area_get_width(&sh_area) / 2) - (a.x1 + first_px);
    if(hor_mid_dist > 0) {
        first_px += hor_mid_dist;
    }
    a.x1 += first_px;

    lv_coord_t ver_mid_dist = (a.y1 + corner_size) - (sh_area.y1 + lv_area_get_height(&sh_area) / 2);
    lv_coord_t ver_mid_corr = 0;
    if(ver_mid_dist <= 0) ver_mid_dist = 0;
    else {
        if(lv_area_get_height(&sh_area) & 0x1) ver_mid_corr = 1;
    }
    lv_opa_t * sh_buf_tmp = sh_buf;

    lv_coord_t y;
    for(y = 0; y < corner_size - ver_mid_dist + ver_mid_corr; y++) {
        memcpy(mask_buf, sh_buf_tmp, corner_size);
        mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
        if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

        lv_blend_fill(clip, &a,
                dsc->shadow_color, mask_buf + first_px, mask_res, opa, dsc->shadow_blend_mode);
        a.y1++;
        a.y2++;
        sh_buf_tmp += corner_size;
    }

    /*Draw the bottom right corner*/
    a.y1 = sh_area.y2;
    a.y2 = a.y1;

    sh_buf_tmp = sh_buf ;

    for(y = 0; y < corner_size - ver_mid_dist; y++) {
        memcpy(mask_buf, sh_buf_tmp, corner_size);
        mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
        if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

        lv_blend_fill(clip, &a,
                dsc->shadow_color, mask_buf + first_px, mask_res, opa, dsc->shadow_blend_mode);
        a.y1--;
        a.y2--;
        sh_buf_tmp += corner_size;
    }

    /*Fill the right side*/
    a.y1 = sh_area.y1 + corner_size;
    a.y2 = a.y1;
    sh_buf_tmp = sh_buf + corner_size * (corner_size - 1);

    lv_coord_t x;

    if(simple_mode) {
        /*Draw vertical lines*/
        lv_area_t va;
        va.x1 = a.x1;
        va.x2 = a.x1;
        va.y1 = sh_area.y1 + corner_size;
        va.y2 = sh_area.y2 - corner_size;

        if(va.y1 <= va.y2) {
            for(x = a.x1; x < a.x2; x++) {
                if(x > coords->x2) {
                    lv_opa_t opa_tmp = sh_buf_tmp[x - a.x1 + first_px];
                    if(opa_tmp != LV_OPA_COVER || opa != LV_OPA_COVER) opa_tmp = (opa * opa_tmp) >> 8;
                    lv_blend_fill(clip, &va,
                            dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa_tmp, dsc->shadow_blend_mode);
                }
                va.x1++;
                va.x2++;
            }
        }
    }
    else {
        for(y = corner_size; y < lv_area_get_height(&sh_area) - corner_size; y++) {
            memcpy(mask_buf, sh_buf_tmp, corner_size);
            mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
            if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

            lv_blend_fill(clip, &a,
                    dsc->shadow_color, mask_buf+first_px, mask_res, opa, dsc->shadow_blend_mode);
            a.y1++;
            a.y2++;
        }
    }

    /*Invert the shadow corner buffer and draw the corners on the left*/
    sh_buf_tmp = sh_buf ;
    for(y = 0; y < corner_size; y++) {
        for(x = 0; x < corner_size / 2; x++) {
            lv_opa_t tmp = sh_buf_tmp[x];
            sh_buf_tmp[x] = sh_buf_tmp[corner_size - x - 1];
            sh_buf_tmp[corner_size - x - 1] = tmp;
        }
        sh_buf_tmp += corner_size;
    }

    /*Draw the top left corner*/
    a.x1 = sh_area.x1;
    a.x2 = a.x1 + corner_size - 1;
    a.y1 = sh_area.y1;
    a.y2 = a.y1;

    if(a.x2 > sh_area.x1 + lv_area_get_width(&sh_area)/2 - 1) {
        a.x2 = sh_area.x1 + lv_area_get_width(&sh_area)/2 -1 ;
    }

    first_px = 0;
    if(disp_area->x1 >= a.x1) {
        first_px = disp_area->x1 - a.x1;
        a.x1 += first_px;
    }

    sh_buf_tmp = sh_buf ;
    for(y = 0; y < corner_size - ver_mid_dist + ver_mid_corr; y++) {
        memcpy(mask_buf, sh_buf_tmp, corner_size);
        mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
        if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

        lv_blend_fill(clip, &a,
                dsc->shadow_color, mask_buf + first_px, mask_res, opa, dsc->shadow_blend_mode);
        a.y1++;
        a.y2++;
        sh_buf_tmp += corner_size;
    }

    /*Draw the bottom left corner*/
    a.y1 = sh_area.y2;
    a.y2 = a.y1;

    sh_buf_tmp = sh_buf ;

    for(y = 0; y < corner_size - ver_mid_dist; y++) {
        memcpy(mask_buf, sh_buf_tmp, corner_size);
        mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
        if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

        lv_blend_fill(clip, &a,
                dsc->shadow_color, mask_buf + first_px, mask_res, opa, dsc->shadow_blend_mode);
        a.y1--;
        a.y2--;
        sh_buf_tmp += corner_size;
    }

    /*Fill the left side*/
    a.y1 = sh_area.y1+corner_size;
    a.y2 = a.y1;

    sh_buf_tmp = sh_buf + corner_size * (corner_size - 1);

    if(simple_mode) {
        /*Draw vertical lines*/
        lv_area_t va;
        va.x1 = a.x1;
        va.x2 = a.x1;
        va.y1 = sh_area.y1 + corner_size;
        va.y2 = sh_area.y2 - corner_size;

        if(va.y1 <= va.y2) {
            for(x = a.x1; x < coords->x1; x++) {
                lv_opa_t opa_tmp = sh_buf_tmp[x - a.x1 + first_px];
                if(opa_tmp != LV_OPA_COVER || opa != LV_OPA_COVER) opa_tmp = (opa * opa_tmp) >> 8;
                lv_blend_fill(clip, &va,
                        dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa_tmp, dsc->shadow_blend_mode);
                va.x1++;
                va.x2++;
            }
        }
    }
    else {
        for(y = corner_size; y < lv_area_get_height(&sh_area) - corner_size; y++) {
            memcpy(mask_buf, sh_buf_tmp, corner_size);
            mask_res = lv_draw_mask_apply(mask_buf + first_px, a.x1, a.y1, lv_area_get_width(&a));
            if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

            lv_blend_fill(clip, &a,
                    dsc->shadow_color, mask_buf + first_px, mask_res, opa, dsc->shadow_blend_mode);
            a.y1++;
            a.y2++;
        }
    }

    /*Fill the top side*/

    a.x1 = sh_area.x1 + corner_size;
    a.x2 = sh_area.x2 - corner_size;
    a.y1 = sh_area.y1;
    a.y2 = a.y1;


    first_px = 0;
    if(disp_area->x1 > a.x1) {
        first_px = disp_area->x1 - a.x1;
        a.x1 += first_px;
    }

    if(a.x1 <= a.x2) {

        sh_buf_tmp = sh_buf + corner_size - 1;

        y_max = corner_size - ver_mid_dist;
        if(simple_mode) {
            y_max = sw / 2 + 1;
            if(y_max > corner_size - ver_mid_dist) y_max = corner_size - ver_mid_dist;
        }

        for(y = 0; y < y_max; y++) {
            if(simple_mode == false) {
                memset(mask_buf, sh_buf_tmp[0], lv_area_get_width(&a));
                mask_res = lv_draw_mask_apply(mask_buf, a.x1, a.y1, lv_area_get_width(&a));
                if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;

                lv_blend_fill(clip, &a,
                        dsc->shadow_color, mask_buf, mask_res, opa, dsc->shadow_blend_mode);
            } else {

                lv_opa_t opa_tmp = sh_buf_tmp[0];
                if(opa_tmp != LV_OPA_COVER || opa != LV_OPA_COVER) opa_tmp = (opa * opa_tmp) >> 8;
                lv_blend_fill(clip, &a,
                        dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa_tmp, dsc->shadow_blend_mode);
            }

            a.y1++;
            a.y2++;
            sh_buf_tmp += corner_size;
        }

        /*Fill the bottom side*/
        lv_coord_t y_min = simple_mode ? (corner_size - (sh_area.y2 - coords->y2)) : ver_mid_dist;
        if(y_min < 0) y_min = 0;
        sh_buf_tmp = sh_buf + corner_size * (corner_size - y_min - 1 ) + corner_size - 1;

        a.y1 = sh_area.y2 - corner_size + 1 + y_min;
        a.y2 = a.y1;

        for(y = y_min; y < corner_size; y++) {
            if(simple_mode == false) {
                memset(mask_buf, sh_buf_tmp[0], lv_area_get_width(&a));
                mask_res = lv_draw_mask_apply(mask_buf, a.x1, a.y1, lv_area_get_width(&a));
                if(mask_res == LV_DRAW_MASK_RES_FULL_COVER) mask_res = LV_DRAW_MASK_RES_CHANGED;
                lv_blend_fill(clip, &a,
                        dsc->shadow_color, mask_buf, mask_res, opa, dsc->shadow_blend_mode);
            } else {
                lv_opa_t opa_tmp = sh_buf_tmp[0];
                if(opa_tmp != LV_OPA_COVER || opa != LV_OPA_COVER) opa_tmp = (opa * opa_tmp) >> 8;
                lv_blend_fill(clip, &a,
                        dsc->shadow_color, NULL, LV_DRAW_MASK_RES_FULL_COVER, opa_tmp, dsc->shadow_blend_mode);
            }

            a.y1++;
            a.y2++;
            sh_buf_tmp -= corner_size;
        }
    }

    /*Finally fill the middle area*/
    if(simple_mode == false) {
        a.y1 = sh_area.y1 + corner_size;
        a.y2 = a.y1;
        if(a.x1 <= a.x2) {
            for(y = 0; y < lv_area_get_height(&sh_area) - corner_size * 2; y++) {
                memset(mask_buf, 0xFF, lv_area_get_width(&a));
                mask_res = lv_draw_mask_apply(mask_buf, a.x1, a.y1, lv_area_get_width(&a));
                lv_blend_fill(clip, &a,
                        dsc->shadow_color, mask_buf, mask_res, opa, dsc->shadow_blend_mode);

                a.y1++;
                a.y2++;
            }
        }
    }

    lv_draw_mask_remove_id(mask_rout_id);
    lv_mem_buf_release(mask_buf);
    lv_mem_buf_release(sh_buf);
}

static void shadow_draw_corner_buf(const lv_area_t * coords, lv_opa_t * sh_buf, lv_coord_t sw, lv_coord_t r)
{
    lv_coord_t sw_ori = sw;
    lv_coord_t size = sw_ori  + r;

    lv_area_t sh_area;
    lv_area_copy(&sh_area, coords);
    sh_area.x2 = sw / 2 + r -1  - (sw & 1 ? 0 : 1);
    sh_area.y1 = sw / 2 + 1;

    sh_area.x1 = sh_area.x2 - lv_area_get_width(coords);
    sh_area.y2 = sh_area.y1 + lv_area_get_height(coords);

    lv_draw_mask_radius_param_t mask_param;
    lv_draw_mask_radius_init(&mask_param, &sh_area, r, false);

#if SHADOW_ENHANCE
    /*Set half shadow width width because blur will be repeated*/
    if(sw_ori == 1) sw = 1;
    else if(sw_ori == 2) sw = 2;
    else if(sw_ori == 3) sw = 2;
    else sw = sw_ori >> 1;
#endif

    lv_draw_mask_res_t mask_res;
    lv_coord_t y;
    lv_opa_t * mask_line = lv_mem_buf_get(size);
    uint16_t * sh_ups_buf = lv_mem_buf_get(size * size * sizeof(uint16_t));
    uint16_t * sh_ups_tmp_buf = sh_ups_buf;
    for(y = 0; y < size; y++) {
        memset(mask_line, 0xFF, size);
        mask_res = mask_param.dsc.cb(mask_line, 0, y, size, &mask_param);
        if(mask_res == LV_DRAW_MASK_RES_FULL_TRANSP) {
            memset(sh_ups_tmp_buf, 0x00, size * sizeof(sh_ups_buf[0]));
        } else {
            lv_coord_t i;
            sh_ups_tmp_buf[0] = (mask_line[0] << SHADOW_UPSACALE_SHIFT) / sw;
            for(i = 1; i < size; i++) {
                if(mask_line[i] == mask_line[i-1]) sh_ups_tmp_buf[i] = sh_ups_tmp_buf[i-1];
                else  sh_ups_tmp_buf[i] = (mask_line[i] << SHADOW_UPSACALE_SHIFT) / sw;
            }
        }

        sh_ups_tmp_buf += size;
    }
    lv_mem_buf_release(mask_line);

    if(sw == 1) {
        lv_coord_t i;
        for(i = 0; i < size * size; i++) {
            sh_buf[i] = (sh_ups_buf[i] >> SHADOW_UPSACALE_SHIFT);
        }
        lv_mem_buf_release(sh_ups_buf);
        return;
    }

    shadow_blur_corner(size, sw, sh_buf, sh_ups_buf);

#if SHADOW_ENHANCE
    sw = sw_ori - sw;
    if(sw <= 1) {
        lv_mem_buf_release(sh_ups_buf);
        return;
    }

    uint32_t i;
    sh_ups_buf[0] = (sh_buf[0] << SHADOW_UPSACALE_SHIFT) / sw;
    for(i = 1; i < (uint32_t) size * size; i++) {
        if(sh_buf[i] == sh_buf[i-1]) sh_ups_buf[i] = sh_ups_buf[i-1];
        else  sh_ups_buf[i] = (sh_buf[i] << SHADOW_UPSACALE_SHIFT) / sw;
    }

    shadow_blur_corner(size, sw, sh_buf, sh_ups_buf);
#endif

    lv_mem_buf_release(sh_ups_buf);
}

static void shadow_blur_corner(lv_coord_t size, lv_coord_t sw, lv_opa_t * res_buf, uint16_t * sh_ups_buf)
{
    lv_coord_t s_left = sw >> 1;
    lv_coord_t s_right = (sw >> 1);
    if((sw & 1) == 0) s_left--;

    /*Horizontal blur*/
    uint16_t * sh_ups_hor_buf = lv_mem_buf_get(size * size * sizeof(uint16_t));
    uint16_t * sh_ups_hor_buf_tmp;

    lv_coord_t x;
    lv_coord_t y;

    uint16_t * sh_ups_tmp_buf = sh_ups_buf;
    sh_ups_hor_buf_tmp = sh_ups_hor_buf;

    for(y = 0; y < size; y++) {
        int32_t v = sh_ups_tmp_buf[size-1] * sw;
        for(x = size - 1; x >=0; x--) {
            sh_ups_hor_buf_tmp[x] = v;

            /*Forget the right pixel*/
            uint32_t right_val = 0;
            if(x + s_right < size) right_val = sh_ups_tmp_buf[x + s_right];
            v -= right_val;

            /*Add the left pixel*/
            uint32_t left_val;
            if(x - s_left - 1 < 0) left_val = sh_ups_tmp_buf[0];
            else left_val = sh_ups_tmp_buf[x - s_left - 1];
            v += left_val;
        }

        sh_ups_tmp_buf += size;
        sh_ups_hor_buf_tmp += size;
    }

    /*Vertical blur*/
    uint32_t i;
    sh_ups_hor_buf[0] = sh_ups_hor_buf[0] / sw;
    for(i = 1; i < (uint32_t)size * size; i++) {
        if(sh_ups_hor_buf[i] == sh_ups_hor_buf[i-1]) sh_ups_hor_buf[i] = sh_ups_hor_buf[i-1];
        else  sh_ups_hor_buf[i] = sh_ups_hor_buf[i] / sw;
    }



    for(x = 0; x < size; x++) {
        sh_ups_hor_buf_tmp = &sh_ups_hor_buf[x];
        lv_opa_t * sh_buf_tmp = &res_buf[x];
        int32_t v = sh_ups_hor_buf_tmp[0] * sw;
        for(y = 0; y < size ; y++, sh_ups_hor_buf_tmp += size, sh_buf_tmp += size) {
            sh_buf_tmp[0] = v < 0 ? 0 : (v >> SHADOW_UPSACALE_SHIFT);

            /*Forget the top pixel*/
            uint32_t top_val;
            if(y - s_right <= 0) top_val = sh_ups_hor_buf_tmp[0];
            else top_val = sh_ups_hor_buf[(y - s_right) * size + x];
            v -= top_val;

            /*Add the bottom pixel*/
            uint32_t bottom_val;
            if(y + s_left + 1 < size) bottom_val = sh_ups_hor_buf[(y + s_left + 1) * size + x];
            else bottom_val = sh_ups_hor_buf[(size - 1) * size + x];
            v += bottom_val;
        }
    }

    lv_mem_buf_release(sh_ups_hor_buf);
}

static void draw_img(const lv_area_t * coords, const lv_area_t * clip, lv_draw_rect_dsc_t * dsc)
{
    if(dsc->pattern_src == NULL) return;
    if(dsc->pattern_opa <= LV_OPA_MIN) return;

    lv_img_src_t src_type = lv_img_src_get_type(dsc->pattern_src);

    lv_draw_img_dsc_t img_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_coord_t img_w;
    lv_coord_t img_h;

    if(src_type == LV_IMG_SRC_FILE || src_type == LV_IMG_SRC_VARIABLE) {
        lv_img_header_t header;
        lv_res_t res = lv_img_decoder_get_info(dsc->pattern_src, &header);
        if(res!= LV_RES_OK) {
            LV_LOG_WARN("draw_img: can't get image info");
            return;
        }

        img_w = header.w;
        img_h = header.h;

        lv_draw_img_dsc_init(&img_dsc);
        img_dsc.opa = dsc->pattern_opa;
        img_dsc.recolor_opa = dsc->pattern_recolor_opa;
        img_dsc.recolor = dsc->pattern_recolor;
    } else if(src_type == LV_IMG_SRC_SYMBOL) {
        lv_draw_label_dsc_init(&label_dsc);
        label_dsc.color = dsc->pattern_recolor;
        label_dsc.font = dsc->pattern_font;
        lv_point_t s;
        lv_txt_get_size(&s, dsc->pattern_src, label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, LV_TXT_FLAG_NONE);
        img_w = s.x;
        img_h = s.y;

    } else {
        /*Trigger the error handler of image drawer*/
        LV_LOG_WARN("lv_img_design: image source type is unknown");
        lv_draw_img(coords, clip, NULL, NULL);
    }



    lv_area_t coords_tmp;

    if(dsc->pattern_repeate) {
        lv_draw_mask_radius_param_t radius_mask_param;
        lv_draw_mask_radius_init(&radius_mask_param, coords, dsc->radius, false);
        int16_t radius_mask_id = lv_draw_mask_add(&radius_mask_param, NULL);

        /*Align the pattern to the middle*/
        lv_coord_t ofs_x = (lv_area_get_width(coords) - (lv_area_get_width(coords) / img_w) * img_w) / 2;
        lv_coord_t ofs_y = (lv_area_get_height(coords) - (lv_area_get_height(coords) / img_h) * img_h) / 2;

        coords_tmp.y1 = coords->y1 - ofs_y;
        coords_tmp.y2 = coords_tmp.y1 + img_h - 1;
        for(; coords_tmp.y1 <= coords->y2; coords_tmp.y1 += img_h, coords_tmp.y2 += img_h) {
            coords_tmp.x1 = coords->x1 - ofs_x;
            coords_tmp.x2 = coords_tmp.x1 + img_w - 1;
            for(; coords_tmp.x1 <= coords->x2; coords_tmp.x1 += img_w, coords_tmp.x2 += img_w) {
                if(src_type == LV_IMG_SRC_SYMBOL)  lv_draw_label(&coords_tmp, clip, &label_dsc, dsc->pattern_src, NULL);
                else lv_draw_img(&coords_tmp, clip, dsc->pattern_src, &img_dsc);
            }
        }
        lv_draw_mask_remove_id(radius_mask_id);
    } else {
        lv_coord_t obj_w = lv_area_get_width(coords);
        lv_coord_t obj_h = lv_area_get_height(coords);
        coords_tmp.x1 = coords->x1 + (obj_w - img_w) / 2;
        coords_tmp.y1 = coords->y1 + (obj_h - img_h) / 2;
        coords_tmp.x2 = coords_tmp.x1 + img_w - 1;
        coords_tmp.y2 = coords_tmp.y1 + img_h - 1;

        /* If the (obj_h - img_h) is odd there is a rounding error when divided by 2.
         * It's better round up in case of symbols because probably there is some extra space in the bottom
         * due to the base line of font*/
        if(src_type == LV_IMG_SRC_SYMBOL) {
            lv_coord_t y_corr = (obj_h - img_h) & 0x1;
            coords_tmp.y1 += y_corr;
            coords_tmp.y2 += y_corr;
        }

        int16_t radius_mask_id = LV_MASK_ID_INV;
        if(lv_area_is_in(&coords_tmp, coords, dsc->radius) == false) {
            lv_draw_mask_radius_param_t radius_mask_param;
            lv_draw_mask_radius_init(&radius_mask_param, coords, dsc->radius, false);
            radius_mask_id = lv_draw_mask_add(&radius_mask_param, NULL);
        }

        if(src_type == LV_IMG_SRC_SYMBOL)  lv_draw_label(&coords_tmp, clip, &label_dsc, dsc->pattern_src, NULL);
        else lv_draw_img(&coords_tmp, clip, dsc->pattern_src, &img_dsc);

        lv_draw_mask_remove_id(radius_mask_id);
    }
}

