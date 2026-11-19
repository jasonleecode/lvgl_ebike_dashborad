#include "road_view.h"
#include <math.h>

enum { MAX_HALF_WIDTH = 13 };
struct ground_point { float right, forward; };
struct scene {
    lv_draw_ctx_t *ctx;
    const struct road_camera *cam;
    struct map_bounds bounds;
    int pass;
};

struct road_camera road_camera_for(const struct map_projection *p, const lv_area_t *box)
{
    double h = lv_area_get_height(box);
    double focal = h * 1.1;
    /* At the vehicle, one pixel spans back/focal metres across the road. */
    double back = focal * MAP_METRES_PER_UNIT / p->py;
    double horizon = box->y1 + h*0.12;
    return (struct road_camera){*p, MAP_METRES_PER_UNIT*p->px/p->py, focal, back, horizon,
                                (box->y1 + h*0.75 - horizon)*back, back*14};
}

static struct ground_point ground(const struct road_camera *c, int x, int y)
{
    double e = (x-c->map.x)*c->east_unit, n = (y-c->map.y)*MAP_METRES_PER_UNIT;
    return (struct ground_point){e*c->map.cos_heading - n*c->map.sin_heading,
                                 e*c->map.sin_heading + n*c->map.cos_heading};
}

struct map_bounds road_visible_bounds(const struct road_camera *c, const lv_area_t *clip)
{
    double min_x=INFINITY, min_y=INFINITY, max_x=-INFINITY, max_y=-INFINITY;
    for (int row=0; row<2; ++row) {
        double y = row ? clip->y2+1.0 : clip->y1-1.0;
        double z = y<=c->horizon ? c->far : fmin(c->far, c->vertical/(y-c->horizon));
        double forward = z-c->back;
        for (int col=0; col<2; ++col) {
            double x = col ? clip->x2+1.0 : clip->x1-1.0;
            double right = (x-c->map.cx)*z/c->focal;
            double e = right*c->map.cos_heading + forward*c->map.sin_heading;
            double n = forward*c->map.cos_heading - right*c->map.sin_heading;
            min_x=fmin(min_x,e); max_x=fmax(max_x,e);
            min_y=fmin(min_y,n); max_y=fmax(max_y,n);
        }
    }
    /* A road centreline just outside the frustum may have visible pavement. */
    return (struct map_bounds){floor(c->map.x+(min_x-MAX_HALF_WIDTH)/c->east_unit),
        floor(c->map.y+(min_y-MAX_HALF_WIDTH)/MAP_METRES_PER_UNIT),
        ceil(c->map.x+(max_x+MAX_HALF_WIDTH)/c->east_unit),
        ceil(c->map.y+(max_y+MAX_HALF_WIDTH)/MAP_METRES_PER_UNIT)};
}

/* Intersect the actual ground polygon with each visible row. This fills
 * concave shorelines and clips roads crossing behind the camera before any
 * conversion to LVGL's small integer screen coordinates. */
static void paint(const struct scene *s, const struct ground_point *v, unsigned count,
                  float *crossings, uint32_t color)
{
    const struct road_camera *c=s->cam;
    const lv_area_t *clip=s->ctx->clip_area;
    float lo=v[0].forward, hi=lo;
    for (unsigned i=1; i<count; ++i) {
        if (v[i].forward<lo) lo=v[i].forward;
        if (v[i].forward>hi) hi=v[i].forward;
    }
    double near=c->vertical/(clip->y2+1-c->horizon);
    double z0=fmax(near,lo+c->back), z1=fmin(c->far,hi+c->back);
    if (z0>=z1) return;
    int top=LV_MAX(clip->y1,(int)ceil(c->horizon+c->vertical/z1-0.5));
    int bottom=LV_MIN(clip->y2,(int)floor(c->horizon+c->vertical/z0-0.5));
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color=lv_color_hex(color);
    for (int y=top; y<=bottom; ++y) {
        double z=c->vertical/(y+0.5-c->horizon);
        float forward=z-c->back;
        unsigned n=0;
        for (unsigned i=0,j=count-1; i<count; j=i++) {
            if ((v[i].forward>forward)==(v[j].forward>forward)) continue;
            float t=(forward-v[i].forward)/(v[j].forward-v[i].forward);
            float x=v[i].right+t*(v[j].right-v[i].right);
            unsigned k=n++;
            while (k && crossings[k-1]>x) { crossings[k]=crossings[k-1]; --k; }
            crossings[k]=x;
        }
        for (unsigned i=1; i<n; i+=2) {
            int x1=LV_MAX(clip->x1,(int)ceil(c->map.cx+crossings[i-1]*c->focal/z-0.5));
            int x2=LV_MIN(clip->x2,(int)ceil(c->map.cx+crossings[i]*c->focal/z-0.5)-1);
            if (x1<=x2) {
                lv_area_t span={x1,y,x2,y};
                lv_draw_rect(s->ctx,&fill,&span);
            }
        }
    }
}

static bool terrain_page(const struct map_page *page, void *user)
{
    struct scene *s=user;
    const struct map_area *a=(const struct map_area *)(page+1);
    for (unsigned i=0; i<page->count; ++i) {
        const uint16_t *offset=(const uint16_t *)(a+1);
        if (map_intersects(a->bounds,s->bounds)) {
            struct ground_point *v=lv_mem_buf_get(a->count*(sizeof(*v)+sizeof(float)));
            if (v) {
                for (unsigned j=0; j<a->count; ++j)
                    v[j]=ground(s->cam,a->bounds.west+offset[2*j],a->bounds.south+offset[2*j+1]);
                paint(s,v,a->count,(float *)(v+a->count),map_area_color(a->kind));
                lv_mem_buf_release(v);
            }
        }
        a=(const struct map_area *)(offset+2*a->count);
    }
    return false;
}

struct road_style { float width; int lanes; uint32_t color; };

/* Display estimates by Garmin class, never a source of turn instructions. */
static struct road_style style_for(int kind, bool one_way)
{
    if (kind==0x16) return (struct road_style){3.0f,1,0x796d56}; /* walking/cycling path */
    if (kind==0x0a) return (struct road_style){4.5f,1,0x796957};
    if (kind==7 || (kind>=8 && kind<=0x0b)) return (struct road_style){4.5f,1,0x59656b};
    int lanes=kind<=2 ? (one_way?3:6) : kind<=5 ? (one_way?2:4) : one_way?1:2;
    return (struct road_style){3.35f*lanes+0.8f,lanes,0x52616a};
}

struct segment_frame {
    struct ground_point start;
    float dx, dy, length;
};

static struct ground_point offset_point(const struct segment_frame *f, float along, float across)
{
    return (struct ground_point){f->start.right+along*f->dx-across*f->dy,
                                 f->start.forward+along*f->dy+across*f->dx};
}

static void ribbon(const struct scene *s, const struct segment_frame *f, float start, float end,
                   float offset, float radius, uint32_t color, bool caps)
{
    /* Bevelled round ends cover the joins of successive road segments. */
    float cap=caps ? radius*0.7f : 0;
    struct ground_point v[]={
        offset_point(f,start-cap,offset-radius*0.7f), offset_point(f,start,offset-radius),
        offset_point(f,end,offset-radius), offset_point(f,end+cap,offset-radius*0.7f),
        offset_point(f,end+cap,offset+radius*0.7f), offset_point(f,end,offset+radius),
        offset_point(f,start,offset+radius), offset_point(f,start-cap,offset+radius*0.7f)
    };
    float crossings[8];
    paint(s,v,8,crossings,color);
}

enum { ROAD_EDGE, ROAD_SURFACE, ROAD_MARKING };

static void draw_segment(const struct scene *s, const struct map_page *page, const struct map_line *line)
{
    int kind=line->kind & MAP_LINE_TYPE;
    if (!map_is_road(kind)) return;
    int x=page->x+line->x1, y=page->y+line->y1;
    int x2=page->x+line->x2, y2=page->y+line->y2;
    struct map_bounds bounds={LV_MIN(x,x2),LV_MIN(y,y2),LV_MAX(x,x2),LV_MAX(y,y2)};
    if (!map_intersects(s->bounds,bounds)) return;
    struct ground_point a=ground(s->cam,x,y), b=ground(s->cam,x2,y2);
    struct segment_frame f={a,b.right-a.right,b.forward-a.forward,0};
    f.length=hypotf(f.dx,f.dy);
    if (f.length<0.01f) return;
    f.dx/=f.length; f.dy/=f.length;
    bool one_way=(line->kind & MAP_LINE_ONEWAY)!=0;
    struct road_style style=style_for(kind,one_way);
    if (s->pass!=ROAD_MARKING) {
        bool edge=s->pass==ROAD_EDGE;
        ribbon(s,&f,0,f.length,0,style.width/2+(edge?0.28f:0),edge?0x9da6a1:style.color,true);
        return;
    }
    float begin=(line->kind & MAP_LINE_JUNCTION_START) ? style.width*0.55f : 0;
    float end=f.length-((line->kind & MAP_LINE_JUNCTION_END) ? style.width*0.55f : 0);
    if (begin>=end) return;
    for (int lane=1; lane<style.lanes; ++lane) {
        float offset=style.width*((float)lane/style.lanes-0.5f);
        if (!one_way && 2*lane==style.lanes)
            ribbon(s,&f,begin,end,offset,0.13f,0xe7b96e,false);
        else {
            for (float d=begin; d<end; d+=8.0f)
                ribbon(s,&f,d,fminf(d+3,end),offset,0.12f,0xdce2de,false);
        }
    }
}

static bool road_page(const struct map_page *page, void *user)
{
    const struct map_line *line=(const struct map_line *)(page+1);
    for (unsigned i=0; i<page->count; ++i) draw_segment(user,page,&line[i]);
    return false;
}

void road_view_draw(struct map_store *store, lv_draw_ctx_t *ctx, const lv_area_t *box,
                    const struct road_camera *cam, int zoom)
{
    const lv_area_t *clip=ctx->clip_area;
    int skyline=(int)ceil(cam->horizon+cam->vertical/cam->far-0.5);
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    lv_area_t sky=*box;
    sky.y2=skyline-1;
    bg.bg_color=lv_color_hex(0x243e51);
    bg.bg_grad.dir=LV_GRAD_DIR_VER;
    bg.bg_grad.stops[0].color=bg.bg_color;
    bg.bg_grad.stops[1].color=lv_color_hex(0x547078);
    lv_draw_rect(ctx,&bg,&sky);
    if (clip->y2<skyline) return;
    lv_area_t land=*box;
    land.y1=skyline;
    bg.bg_grad.dir=LV_GRAD_DIR_NONE;
    bg.bg_color=lv_color_hex(0x233331);
    lv_draw_rect(ctx,&bg,&land);
    struct scene scene={ctx,cam,road_visible_bounds(cam,clip),0};
    map_store_select(store,road_visible_bounds(cam,box));
    map_store_visit(store,MAP_AREA,scene.bounds,zoom,terrain_page,&scene);
    /* Complete each layer across all tile pages so roads join at tile boundaries. */
    for (int pass=ROAD_EDGE; pass<=ROAD_MARKING; ++pass) {
        scene.pass=pass;
        map_store_visit(store,MAP_LINE,scene.bounds,zoom,road_page,&scene);
    }
}
