#include "car_breaker19_scene_about.h"
#include "../version.h"
#include <gui/elements.h>
#include <gui/view_dispatcher.h>
#include <furi.h>
#include <furi_hal.h>

#define ANIMATION_FRAMES 12
#define ANIMATION_SPEED_MS 200

#define MAX_PARTICLES 8

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    uint8_t life;
} Particle;

static const char* border_frames[ANIMATION_FRAMES][5] = {
    {"*                 *",
     "                   ",
     "                   ",
     "                   ",
     "*                 *"},
    {" *               * ",
     "                   ",
     "                   ",
     "                   ",
     " *               * "},
    {" *---------------* ",
     "                   ",
     "                   ",
     "                   ",
     " *---------------* "},
    {" *---------------* ",
     " |               | ",
     " |               | ",
     " |               | ",
     " *---------------* "},
    {" +===============+ ",
     " |               | ",
     " |               | ",
     " |               | ",
     " +===============+ "},
    {" #===============# ",
     " |               | ",
     " |               | ",
     " |               | ",
     " #===============# "},
    {"  .=============.  ",
     "  |             |  ",
     "  |             |  ",
     "  |             |  ",
     "  .=============.  "},
    {"> .=============. <",
     "  |             |  ",
     "  |             |  ",
     "  |             |  ",
     "> .=============. <"},
    {" >.===========.<  ",
     " ||           ||  ",
     " ||           ||  ",
     " ||           ||  ",
     " >.===========.<  "},
    {"~~~~~~~~~~~~~~~~~~",
     " |              | ",
     " |              | ",
     " |              | ",
     "~~~~~~~~~~~~~~~~~~"},
    {"  *-----------*   ",
     "  |           |   ",
     "  |           |   ",
     "  |           |   ",
     "  *-----------*   "},
    {" *             *  ",
     "                  ",
     "                  ",
     "                  ",
     " *             *  "},
};

struct CarBreaker19SceneAbout {
    View* view;
    FuriTimer* timer;
};

typedef struct {
    uint8_t frame;
    Particle particles[MAX_PARTICLES];
    uint8_t particle_spawn_counter;
    struct CarBreaker19SceneAbout* instance;
} CarBreaker19SceneAboutModel;

static void car_breaker19_scene_about_timer_callback(void* context) {
    furi_assert(context);
    struct CarBreaker19SceneAbout* instance = context;
    with_view_model(
        instance->view,
        CarBreaker19SceneAboutModel * model,
        {
            model->frame = (model->frame + 1) % ANIMATION_FRAMES;

            for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
                if(model->particles[i].life > 0) {
                    model->particles[i].x += model->particles[i].vx;
                    model->particles[i].y += model->particles[i].vy;
                    model->particles[i].life--;

                    if(model->particles[i].x < 0 || model->particles[i].x > 127) {
                        model->particles[i].vx = -model->particles[i].vx;
                    }
                    if(model->particles[i].y < 0 || model->particles[i].y > 63) {
                        model->particles[i].vy = -model->particles[i].vy;
                    }
                }
            }

            model->particle_spawn_counter++;
            if(model->particle_spawn_counter >= 3) {
                model->particle_spawn_counter = 0;
                for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
                    if(model->particles[i].life == 0) {
                        uint8_t corner = (furi_hal_random_get() % 4);
                        model->particles[i].x = (corner & 1) ? 120.0f : 8.0f;
                        model->particles[i].y = (corner & 2) ? 55.0f : 15.0f;
                        model->particles[i].vx = ((furi_hal_random_get() % 100) - 50) / 50.0f;
                        model->particles[i].vy = ((furi_hal_random_get() % 100) - 50) / 50.0f;
                        model->particles[i].life = 20 + (furi_hal_random_get() % 20);
                        break;
                    }
                }
            }
        },
        true);
}

static void car_breaker19_scene_about_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    CarBreaker19SceneAboutModel* model = context;

    canvas_clear(canvas);

    for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
        if(model->particles[i].life > 0) {
            uint8_t size = (model->particles[i].life > 10) ? 2 : 1;
            canvas_draw_disc(
                canvas, (uint8_t)model->particles[i].x, (uint8_t)model->particles[i].y, size);
        }
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Car Breaker 19");

    uint8_t frame = model->frame;

    canvas_set_font(canvas, FontSecondary);
    const uint8_t box_x = 5;
    const uint8_t box_y = 18;
    const uint8_t line_height = 10;

    canvas_draw_str(canvas, box_x, box_y, border_frames[frame][0]);
    canvas_draw_str(canvas, box_x, box_y + line_height, border_frames[frame][1]);
    canvas_draw_str(canvas, box_x, box_y + line_height * 2, border_frames[frame][2]);
    canvas_draw_str(canvas, box_x, box_y + line_height * 3, border_frames[frame][3]);
    canvas_draw_str(canvas, box_x, box_y + line_height * 4, border_frames[frame][4]);

    canvas_set_font(canvas, FontSecondary);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 18, 28, 92, 26);
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignCenter, "Honda/Mini RKE Analyzer");
    canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "Passive + Guided Audit");

    canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignCenter, APP_VERSION_STR);
}

static bool car_breaker19_scene_about_input_callback(InputEvent* event, void* context) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void car_breaker19_scene_about_enter_callback(void* context) {
    furi_assert(context);
    struct CarBreaker19SceneAbout* instance = context;
    with_view_model(
        instance->view,
        CarBreaker19SceneAboutModel * model,
        {
            model->frame = 0;
            model->particle_spawn_counter = 0;
            for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
                model->particles[i].life = 0;
            }
        },
        true);
    furi_timer_start(instance->timer, ANIMATION_SPEED_MS);
}

static void car_breaker19_scene_about_exit_callback(void* context) {
    furi_assert(context);
    struct CarBreaker19SceneAbout* instance = context;
    furi_timer_stop(instance->timer);
}

View* car_breaker19_scene_about_alloc(void) {
    struct CarBreaker19SceneAbout* instance = malloc(sizeof(struct CarBreaker19SceneAbout));
    if(instance == NULL) return NULL;

    instance->view = view_alloc();
    if(instance->view == NULL) {
        free(instance);
        return NULL;
    }
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(CarBreaker19SceneAboutModel));
    view_set_draw_callback(instance->view, car_breaker19_scene_about_draw_callback);
    view_set_enter_callback(instance->view, car_breaker19_scene_about_enter_callback);
    view_set_exit_callback(instance->view, car_breaker19_scene_about_exit_callback);
    view_set_input_callback(instance->view, car_breaker19_scene_about_input_callback);

    with_view_model(
        instance->view,
        CarBreaker19SceneAboutModel * model,
        {
            model->frame = 0;
            model->particle_spawn_counter = 0;
            model->instance = instance;
            for(uint8_t i = 0; i < MAX_PARTICLES; i++) {
                model->particles[i].life = 0;
            }
        },
        true);

    instance->timer = furi_timer_alloc(
        car_breaker19_scene_about_timer_callback, FuriTimerTypePeriodic, instance);
    if(instance->timer == NULL) {
        view_free(instance->view);
        free(instance);
        return NULL;
    }

    return instance->view;
}

void car_breaker19_scene_about_free(View* view) {
    furi_assert(view);
    struct CarBreaker19SceneAbout* instance = NULL;
    with_view_model(
        view,
        CarBreaker19SceneAboutModel * model,
        { instance = model->instance; },
        false);
    if(instance) {
        furi_timer_free(instance->timer);
        free(instance);
    }
    view_free(view);
}
