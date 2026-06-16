#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define MAX_FIREWORKS 30
#define MAX_PARTICLES 500
#define MAX_ERROR_MSG 256

typedef enum {
    FIREWORK_CHRYSANTHEMUM = 0,
    FIREWORK_WILLOW = 1,
    FIREWORK_RING = 2,
    FIREWORK_FOUNTAIN = 3,
    FIREWORK_PEARL_CHAIN = 4,
    FIREWORK_TYPE_COUNT = 5
} FireworkType;

typedef struct {
    FireworkType type;
    int particle_count;
    float explosion_radius;
    float life;
    int color_variation;
} FireworkConfig;

typedef struct {
    float x, y;
    float vx, vy;
    int r, g, b, a;
    float life;
    float max_life;
    int active;
    float base_hue;
} Particle;

typedef struct {
    float x, y;
    float targetX, targetY;
    float vx, vy;
    int r, g, b;
    int exploded;
    int active;
    FireworkConfig config;
    float base_hue;
    int pearl_stage;
    float pearl_timer;
    Particle particles[MAX_PARTICLES];
} Firework;

FireworkConfig g_current_config = {
    FIREWORK_CHRYSANTHEMUM,
    150,
    6.0f,
    60.0f,
    30
};

FireworkConfig g_default_config = {
    FIREWORK_CHRYSANTHEMUM,
    150,
    6.0f,
    60.0f,
    30
};

char g_last_error[MAX_ERROR_MSG] = {0};

const FireworkConfig g_type_defaults[FIREWORK_TYPE_COUNT] = {
    {FIREWORK_CHRYSANTHEMUM, 150, 6.0f, 60.0f, 30},
    {FIREWORK_WILLOW, 200, 4.0f, 120.0f, 15},
    {FIREWORK_RING, 100, 7.0f, 50.0f, 0},
    {FIREWORK_FOUNTAIN, 80, 3.0f, 80.0f, 45},
    {FIREWORK_PEARL_CHAIN, 60, 5.0f, 70.0f, 60}
};

SDL_Window *window;
SDL_Renderer *renderer;
Firework fireworks[MAX_FIREWORKS];
int screen_width = 0;
int screen_height = 0;

float random_float(float min, float max) {
    return min + (float)rand() / ((float)RAND_MAX / (max - min));
}

// Convert HSL to RGB
void hsl_to_rgb(float h, float s, float l, int *r, int *g, int *b) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - 0.5f * c;
    float r_temp, g_temp, b_temp;

    if (h >= 0 && h < 60) { r_temp = c; g_temp = x; b_temp = 0; }
    else if (h >= 60 && h < 120) { r_temp = x; g_temp = c; b_temp = 0; }
    else if (h >= 120 && h < 180) { r_temp = 0; g_temp = c; b_temp = x; }
    else if (h >= 180 && h < 240) { r_temp = 0; g_temp = x; b_temp = c; }
    else if (h >= 240 && h < 300) { r_temp = x; g_temp = 0; b_temp = c; }
    else { r_temp = c; g_temp = 0; b_temp = x; }

    *r = (int)((r_temp + m) * 255);
    *g = (int)((g_temp + m) * 255);
    *b = (int)((b_temp + m) * 255);
}

int validate_and_apply_config(FireworkConfig *config) {
    g_last_error[0] = '\0';
    char error_buf[MAX_ERROR_MSG] = {0};
    int has_error = 0;

    if (config->type < 0 || config->type >= FIREWORK_TYPE_COUNT) {
        snprintf(error_buf, MAX_ERROR_MSG, "type: 非法值 %d，范围应为 0-%d。", config->type, FIREWORK_TYPE_COUNT - 1);
        strncat(g_last_error, error_buf, MAX_ERROR_MSG - strlen(g_last_error) - 1);
        config->type = g_default_config.type;
        has_error = 1;
    }

    FireworkConfig type_default = g_type_defaults[config->type];

    if (config->particle_count < 10 || config->particle_count > MAX_PARTICLES) {
        snprintf(error_buf, MAX_ERROR_MSG, "%sparticle_count: 非法值 %d，范围应为 10-%d。", has_error ? " " : "", config->particle_count, MAX_PARTICLES);
        strncat(g_last_error, error_buf, MAX_ERROR_MSG - strlen(g_last_error) - 1);
        config->particle_count = type_default.particle_count;
        has_error = 1;
    }

    if (config->explosion_radius < 1.0f || config->explosion_radius > 15.0f) {
        snprintf(error_buf, MAX_ERROR_MSG, "%sexplosion_radius: 非法值 %.2f，范围应为 1.0-15.0。", has_error ? " " : "", config->explosion_radius);
        strncat(g_last_error, error_buf, MAX_ERROR_MSG - strlen(g_last_error) - 1);
        config->explosion_radius = type_default.explosion_radius;
        has_error = 1;
    }

    if (config->life < 20.0f || config->life > 200.0f) {
        snprintf(error_buf, MAX_ERROR_MSG, "%slife: 非法值 %.2f，范围应为 20.0-200.0。", has_error ? " " : "", config->life);
        strncat(g_last_error, error_buf, MAX_ERROR_MSG - strlen(g_last_error) - 1);
        config->life = type_default.life;
        has_error = 1;
    }

    if (config->color_variation < 0 || config->color_variation > 180) {
        snprintf(error_buf, MAX_ERROR_MSG, "%scolor_variation: 非法值 %d，范围应为 0-180。", has_error ? " " : "", config->color_variation);
        strncat(g_last_error, error_buf, MAX_ERROR_MSG - strlen(g_last_error) - 1);
        config->color_variation = type_default.color_variation;
        has_error = 1;
    }

    return has_error;
}

void init_particle_for_type(Particle *p, float x, float y, FireworkConfig *config, float base_hue, int index, int total) {
    p->x = x;
    p->y = y;
    p->base_hue = base_hue;
    p->a = 255;
    p->active = 1;

    float hue = base_hue;
    if (config->color_variation > 0) {
        hue = base_hue + random_float(-config->color_variation, config->color_variation);
        if (hue < 0) hue += 360;
        if (hue >= 360) hue -= 360;
    }
    hsl_to_rgb(hue, 1.0f, 0.5f, &p->r, &p->g, &p->b);

    float life_variation = config->life * 0.2f;
    p->life = config->life + random_float(-life_variation, life_variation);
    if (p->life < 10.0f) p->life = 10.0f;
    p->max_life = p->life;

    float angle, speed;
    float speed_min = config->explosion_radius * 0.3f;
    float speed_max = config->explosion_radius;

    switch (config->type) {
        case FIREWORK_CHRYSANTHEMUM:
            angle = random_float(0, M_PI * 2);
            speed = random_float(speed_min, speed_max);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            break;

        case FIREWORK_WILLOW:
            angle = random_float(0, M_PI * 2);
            speed = random_float(speed_min * 0.5f, speed_max * 0.7f);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed - 1.0f;
            break;

        case FIREWORK_RING:
            angle = (float)index / (float)total * M_PI * 2;
            speed = speed_max * 0.8f;
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            break;

        case FIREWORK_FOUNTAIN:
            angle = random_float(-M_PI * 0.7f, -M_PI * 0.3f);
            speed = random_float(speed_min, speed_max);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            break;

        case FIREWORK_PEARL_CHAIN:
            angle = (float)index / (float)total * M_PI * 2;
            speed = random_float(speed_min * 0.6f, speed_max * 0.8f);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            break;

        default:
            angle = random_float(0, M_PI * 2);
            speed = random_float(speed_min, speed_max);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            break;
    }
}

void init_firework(Firework *f) {
    f->x = random_float(100, screen_width - 100);
    f->y = screen_height;
    f->targetX = f->x + random_float(-100, 100);
    f->targetY = random_float(100, screen_height * 0.4);

    float angle = atan2(f->targetY - f->y, f->targetX - f->x);
    float speed = random_float(10.0f, 15.0f);

    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed;

    f->base_hue = random_float(0, 360);
    hsl_to_rgb(f->base_hue, 1.0f, 0.5f, &f->r, &f->g, &f->b);

    f->config = g_current_config;
    f->pearl_stage = 0;
    f->pearl_timer = 0;
    f->exploded = 0;
    f->active = 1;
}

void explode_firework(Firework *f) {
    int count = f->config.particle_count;
    if (count > MAX_PARTICLES) count = MAX_PARTICLES;

    if (f->config.type == FIREWORK_PEARL_CHAIN) {
        f->pearl_stage = 1;
        f->pearl_timer = 0;
        int first_count = count / 3;
        for (int j = 0; j < first_count; j++) {
            init_particle_for_type(&f->particles[j], f->x, f->y, &f->config, f->base_hue, j, first_count);
        }
        for (int j = first_count; j < MAX_PARTICLES; j++) {
            f->particles[j].active = 0;
        }
    } else {
        for (int j = 0; j < count; j++) {
            init_particle_for_type(&f->particles[j], f->x, f->y, &f->config, f->base_hue, j, count);
        }
        for (int j = count; j < MAX_PARTICLES; j++) {
            f->particles[j].active = 0;
        }
    }
}

void update_pearl_chain(Firework *f) {
    f->pearl_timer += 1.0f;

    if (f->pearl_stage == 1 && f->pearl_timer >= 15.0f) {
        f->pearl_stage = 2;
        f->pearl_timer = 0;
        int count = f->config.particle_count;
        int first_count = count / 3;
        int second_count = count / 3;
        float new_hue = f->base_hue + 60.0f;
        if (new_hue >= 360) new_hue -= 360;
        FireworkConfig temp_config = f->config;
        temp_config.explosion_radius *= 1.3f;
        for (int j = first_count; j < first_count + second_count && j < MAX_PARTICLES; j++) {
            init_particle_for_type(&f->particles[j], f->x, f->y, &temp_config, new_hue, j - first_count, second_count);
        }
    } else if (f->pearl_stage == 2 && f->pearl_timer >= 15.0f) {
        f->pearl_stage = 3;
        f->pearl_timer = 0;
        int count = f->config.particle_count;
        int first_two = count * 2 / 3;
        int third_count = count - first_two;
        float new_hue = f->base_hue + 120.0f;
        if (new_hue >= 360) new_hue -= 360;
        FireworkConfig temp_config = f->config;
        temp_config.explosion_radius *= 1.6f;
        for (int j = first_two; j < first_two + third_count && j < MAX_PARTICLES; j++) {
            init_particle_for_type(&f->particles[j], f->x, f->y, &temp_config, new_hue, j - first_two, third_count);
        }
    }
}

void update() {
    if (rand() % 10 == 0) {
        for (int i = 0; i < MAX_FIREWORKS; i++) {
            if (!fireworks[i].active) {
                init_firework(&fireworks[i]);
                break;
            }
        }
    }

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) continue;

        Firework *f = &fireworks[i];

        if (!f->exploded) {
            f->x += f->vx;
            f->y += f->vy;
            f->vy += 0.15f;

            if (f->vy >= 0 || f->y <= f->targetY) {
                f->exploded = 1;
                explode_firework(f);
            }
        } else {
            if (f->config.type == FIREWORK_PEARL_CHAIN && f->pearl_stage < 3) {
                update_pearl_chain(f);
            }

            int active_particles = 0;
            for (int j = 0; j < MAX_PARTICLES; j++) {
                if (f->particles[j].active) {
                    active_particles++;
                    Particle *p = &f->particles[j];

                    p->x += p->vx;
                    p->y += p->vy;
                    p->vx *= 0.96f;
                    p->vy *= 0.96f;

                    if (f->config.type == FIREWORK_WILLOW) {
                        p->vy += 0.25f;
                    } else {
                        p->vy += 0.15f;
                    }

                    p->life -= 1.0f;

                    float alpha_ratio = p->life / p->max_life;
                    p->a = (int)(alpha_ratio * 255);

                    if (p->life <= 0) {
                        p->active = 0;
                    }
                }
            }
            if (active_particles == 0 && f->pearl_stage >= 3) {
                f->active = 0;
            } else if (active_particles == 0 && f->config.type != FIREWORK_PEARL_CHAIN) {
                f->active = 0;
            }
        }
    }
}

void draw() {
    // Create trail effect with semi-transparent black
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 25); // Lower alpha = longer trails
    SDL_Rect rect = {0, 0, screen_width, screen_height};
    SDL_RenderFillRect(renderer, &rect);

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) continue;
        
        Firework *f = &fireworks[i];
        
        if (!f->exploded) {
            // Draw rising firework (rocket)
            SDL_SetRenderDrawColor(renderer, f->r, f->g, f->b, 255);
            SDL_Rect r = {(int)f->x - 2, (int)f->y - 2, 4, 4};
            SDL_RenderFillRect(renderer, &r);
        } else {
            // Draw explosion particles
            for (int j = 0; j < MAX_PARTICLES; j++) {
                if (f->particles[j].active) {
                    Particle *p = &f->particles[j];
                    SDL_SetRenderDrawColor(renderer, p->r, p->g, p->b, p->a);
                    // Draw slightly larger particles for better visibility
                    SDL_Rect p_rect = {(int)p->x - 1, (int)p->y - 1, 3, 3};
                    SDL_RenderFillRect(renderer, &p_rect);
                }
            }
        }
    }

    SDL_RenderPresent(renderer);
}

void main_loop() {
    update();
    draw();
}

EMSCRIPTEN_KEEPALIVE
int set_firework_type(int type) {
    FireworkConfig config = g_current_config;
    config.type = type;
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
int set_firework_particle_count(int count) {
    FireworkConfig config = g_current_config;
    config.particle_count = count;
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
int set_firework_explosion_radius(float radius) {
    FireworkConfig config = g_current_config;
    config.explosion_radius = radius;
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
int set_firework_life(float life) {
    FireworkConfig config = g_current_config;
    config.life = life;
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
int set_firework_color_variation(int variation) {
    FireworkConfig config = g_current_config;
    config.color_variation = variation;
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
int set_firework_config(int type, int particle_count, float explosion_radius, float life, int color_variation) {
    FireworkConfig config = {
        (FireworkType)type,
        particle_count,
        explosion_radius,
        life,
        color_variation
    };
    int result = validate_and_apply_config(&config);
    g_current_config = config;
    return result;
}

EMSCRIPTEN_KEEPALIVE
const char* get_last_error() {
    return g_last_error;
}

EMSCRIPTEN_KEEPALIVE
void reset_to_defaults() {
    g_current_config = g_default_config;
    g_last_error[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE
void use_type_defaults(int type) {
    if (type >= 0 && type < FIREWORK_TYPE_COUNT) {
        g_current_config = g_type_defaults[type];
    } else {
        g_current_config = g_default_config;
    }
    g_last_error[0] = '\0';
}

EMSCRIPTEN_KEEPALIVE
int get_current_type() {
    return g_current_config.type;
}

EMSCRIPTEN_KEEPALIVE
int get_current_particle_count() {
    return g_current_config.particle_count;
}

EMSCRIPTEN_KEEPALIVE
float get_current_explosion_radius() {
    return g_current_config.explosion_radius;
}

EMSCRIPTEN_KEEPALIVE
float get_current_life() {
    return g_current_config.life;
}

EMSCRIPTEN_KEEPALIVE
int get_current_color_variation() {
    return g_current_config.color_variation;
}

int main() {
    srand(time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    double w, h;
    emscripten_get_element_css_size("#canvas", &w, &h);
    screen_width = (int)w;
    screen_height = (int)h;

    SDL_CreateWindowAndRenderer(screen_width, screen_height, 0, &window, &renderer);
    
    // Initialize fireworks pool
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        fireworks[i].active = 0;
    }

    emscripten_set_main_loop(main_loop, 0, 1);

    return 0;
}
