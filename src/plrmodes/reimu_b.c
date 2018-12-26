/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2018, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2018, Andrei Alexeyev <akari@alienslab.net>.
 */

#include "taisei.h"

#include "global.h"
#include "plrmodes.h"
#include "reimu.h"
#include "stagedraw.h"
#include "util/glm.h"

#define GAP_LENGTH 128
#define GAP_WIDTH 16

#define GAP_OFFSET 8
#define GAP_LIMIT 10

#define NUM_GAPS 4
#define FOR_EACH_GAP(gap) for(Enemy *gap = global.plr.slaves.first; gap; gap = gap->next) if(gap->logic_rule == reimu_dream_gap)

static Enemy *gap_renderer;
static VertexArray *gap_varray;
static VertexBuffer *gap_vbuffer;

typedef struct GapRenderAttribs {
	struct {
		float pos_x;
		float pos_y;
		float view_x;
		float view_y;
	} coords;

	struct {
		float current;
		float linked;
	} angles;
} GapRenderAttribs;

static complex reimu_dream_gap_target_pos(Enemy *e) {
	double x, y;

	if(creal(e->pos0)) {
		x = creal(e->pos0) * VIEWPORT_W;
	} else {
		x = creal(global.plr.pos);
	}

	if(cimag(e->pos0)) {
		y = cimag(e->pos0) * VIEWPORT_H;
	} else {
		y = cimag(global.plr.pos);
	}

	bool focus = global.plr.inputflags & INFLAG_FOCUS;

	// complex ofs = GAP_OFFSET * (1 + I) + (GAP_LENGTH * 0.5 + GAP_LIMIT) * e->args[0];
	double ofs_x = GAP_OFFSET + (GAP_LENGTH * 0.5 + GAP_LIMIT) * creal(e->args[0]);
	double ofs_y = GAP_OFFSET + (!focus) * (GAP_LENGTH * 0.5 + GAP_LIMIT) * cimag(e->args[0]);

	x = clamp(x, ofs_x, VIEWPORT_W - ofs_x);
	y = clamp(y, ofs_y, VIEWPORT_H - ofs_y);

	if(focus) {
		/*
		if(cimag(e->pos0)) {
			x = VIEWPORT_W - x;
		}
		*/
	} else {
		if(creal(e->pos0)) {
			y = VIEWPORT_H - y;
		}
	}

	return x + I * y;
}

static int reimu_dream_gap_bomb_projectile(Projectile *p, int t) {
	if(t == EVENT_BIRTH) {
		return ACTION_ACK;
	}

	if(t == EVENT_DEATH) {
		return ACTION_ACK;
	}

	p->pos += p->args[0];

	if(!(t % 3)) {
		double range = GAP_LENGTH * 0.35;
		double damage = 50;
		// Yes, I know, this is inefficient as hell, but I'm too lazy to write a
		// stage_clear_hazards_inside_rectangle function.
		stage_clear_hazards_at(p->pos, range, CLEAR_HAZARDS_ALL | CLEAR_HAZARDS_NOW);
		ent_area_damage(p->pos, range, &(DamageInfo) { damage, DMG_PLAYER_BOMB });
	}

	return ACTION_NONE;
}

static void reimu_dream_gap_bomb_projectile_draw(Projectile *p, int t) {
	r_draw_sprite(&(SpriteParams) {
		.sprite_ptr = p->sprite,
		.shader_ptr = p->shader,
		.color = &p->color,
		.shader_params = &p->shader_params,
		.pos = { creal(p->pos), cimag(p->pos) },
		.scale.both = 0.75 * clamp(t / 5.0, 0.1, 1.0),
	});
}

static void reimu_dream_gap_bomb(Enemy *e, int t) {
	if(!(t % 4)) {
		PROJECTILE(
			.sprite = "glowball",
			.size = 32 * (1 + I),
			.color = HSLA(t/30.0, 0.5, 0.5, 0.5),
			.pos = e->pos + e->args[0] * (frand() - 0.5) * GAP_LENGTH * 0.5,
			.rule = reimu_dream_gap_bomb_projectile,
			.draw_rule = reimu_dream_gap_bomb_projectile_draw,
			.type = PlrProj,
			.damage_type = DMG_PLAYER_BOMB,
			.damage = 75,
			.args = { -20 * e->pos0 },
		);

		global.shake_view += 5;

		if(!(t % 16)) {
			play_sound("boon");
		}
	}
}

static int reimu_dream_gap(Enemy *e, int t) {
	if(t == EVENT_DEATH) {
		free_ref(creal(e->args[3]));
		return ACTION_ACK;
	}

	if(t == EVENT_BIRTH) {
		return ACTION_ACK;
	}

	if(player_is_bomb_active(&global.plr)) {
		reimu_dream_gap_bomb(e, t + cimag(e->args[3]));
	} else {
		complex new_pos = reimu_dream_gap_target_pos(e);

		if(t == 0) {
			e->pos = new_pos;
		} else {
			e->pos += (new_pos - e->pos) * 0.1 * (1 - sqrt(gap_renderer->args[0]));
		}
	}

	return ACTION_NONE;
}

static void reimu_dream_gap_link(Enemy *g0, Enemy *g1) {
	int ref0 = add_ref(g0);
	int ref1 = add_ref(g1);
	g0->args[3] = ref1;
	g1->args[3] = ref0;
}

static Enemy* reimu_dream_gap_get_linked(Enemy *gap) {
	return REF(creal(gap->args[3]));
}

static void reimu_dream_gap_draw_lights(int time, double strength) {
	if(strength <= 0) {
		return;
	}

	r_shader("reimu_gap_light");
	r_uniform_sampler("tex", "gaplight");
	r_uniform_float("time", time / 60.0);
	r_uniform_float("strength", strength);

	FOR_EACH_GAP(gap) {
		const float len = GAP_LENGTH * 3 * sqrt(log(strength + 1) / 0.693);
		complex center = gap->pos - gap->pos0 * (len * 0.5 - GAP_WIDTH * 0.6);

		r_mat_push();
		r_mat_translate(creal(center), cimag(center), 0);
		r_mat_rotate(carg(gap->pos0)+M_PI, 0, 0, 1);
		r_mat_scale(len, GAP_LENGTH, 1);
		r_draw_quad();
		r_mat_pop();
	}
}

static void reimu_dream_gap_renderer_visual(Enemy *e, int t, bool render) {
	if(!render) {
		return;
	}

	FBPair *framebuffers = stage_get_fbpair(FBPAIR_FG);

	fbpair_swap(framebuffers);
	r_framebuffer(framebuffers->back);
	r_shader_standard();
	draw_framebuffer_tex(framebuffers->front, VIEWPORT_W, VIEWPORT_H);
	fbpair_swap(framebuffers);
	r_framebuffer(framebuffers->back);

	r_shader("reimu_gap");

	r_vertex_buffer_invalidate(gap_vbuffer);
	SDL_RWops *stream = r_vertex_buffer_get_stream(gap_vbuffer);

	r_uniform_sampler("tex", r_framebuffer_get_attachment(framebuffers->front, FRAMEBUFFER_ATTACH_COLOR0));
	r_uniform_float("time", t / (float)FPS);
	r_uniform_vec2("viewport", VIEWPORT_W, VIEWPORT_H);
	r_uniform_vec2("gap_size", GAP_LENGTH, GAP_WIDTH);

	FOR_EACH_GAP(gap) {
		GapRenderAttribs attrs = { 0 };
		Enemy *linked_gap = reimu_dream_gap_get_linked(gap);

		attrs.coords.pos_x = creal(gap->pos);
		attrs.coords.pos_y = cimag(gap->pos);
		attrs.coords.view_x = creal(linked_gap->pos);
		attrs.coords.view_y = cimag(linked_gap->pos);
		attrs.angles.current = carg(gap->pos0) - M_PI/2;
		attrs.angles.linked = carg(linked_gap->pos0) - M_PI/2;

		SDL_RWwrite(stream, &attrs, sizeof(attrs), 1);
	}

	r_draw(gap_varray, PRIM_TRIANGLE_STRIP, 0, 4, NUM_GAPS, 0);

	FOR_EACH_GAP(gap) {
		r_mat_push();
		r_mat_translate(creal(gap->pos), cimag(gap->pos), 0);
		complex stretch_vector = gap->args[0];

		for(float ofs = -0.5; ofs <= 0.5; ofs += 1) {
			r_draw_sprite(&(SpriteParams) {
				.sprite = "yinyang",
				.shader = "sprite_yinyang",
				.pos = {
					creal(stretch_vector) * GAP_LENGTH * ofs,
					cimag(stretch_vector) * GAP_LENGTH * ofs
				},
				.rotation.angle = global.frames * -6 * DEG2RAD,
				.color = RGB(0.95, 0.75, 1.0),
				.scale.both = 0.5,
			});
		}

		r_mat_pop();
	}

	reimu_dream_gap_draw_lights(t, pow(e->args[0], 2));
}

static int reimu_dream_gap_renderer(Enemy *e, int t) {
	if(t < 0) {
		return ACTION_ACK;
	}

	if(player_is_bomb_active(&global.plr)) {
		e->args[0] = approach(e->args[0], 1.0, 0.1);
	} else {
		e->args[0] = approach(e->args[0], 0.0, 0.025);
	}

	return ACTION_NONE;
}

static void reimu_dream_preload(void) {
	const int flags = RESF_DEFAULT;

	preload_resources(RES_SPRITE, flags,
		"yinyang",
		"proj/ofuda",
		"proj/needle2",
		"proj/glowball",
		"part/myon",
		"part/stardust",
	NULL);

	preload_resources(RES_TEXTURE, flags,
		"runes",
		"gaplight",
	NULL);

	preload_resources(RES_SHADER_PROGRAM, flags,
		"sprite_yinyang",
		"reimu_gap",
		"reimu_gap_light",
		"reimu_bomb_bg",
	NULL);

	preload_resources(RES_SFX, flags | RESF_OPTIONAL,
		"bomb_marisa_a",
		"boon",
	NULL);
}

static void reimu_dream_bomb(Player *p) {
	play_sound("bomb_marisa_a");
}

static void reimu_dream_bomb_bg(Player *p) {
	float a = gap_renderer->args[0];
	reimu_common_bomb_bg(p, a);
}


static void reimu_dream_spawn_warp_effect(complex pos, bool exit) {
	PARTICLE(
		.sprite = "myon",
		.pos = pos,
		.color = RGBA(0.5, 0.5, 0.5, 0.5),
		.timeout = 20,
		.angle = frand() * M_PI * 2,
		.draw_rule = ScaleFade,
		.args = { 0, 0, 1 + 2 * I },
		.layer = LAYER_PLAYER_FOCUS,
	);

	PARTICLE(
		.sprite = exit ? "stain" : "stardust",
		.pos = pos,
		.color = color_mul_scalar(RGBA(0.75, 0.4 * frand(), 0.4, 0), 0.8),
		.timeout = 20,
		.angle = frand() * M_PI * 2,
		.draw_rule = ScaleFade,
		.args = { 0, 0, 0.25 + 1.5 * I },
		.layer = LAYER_PLAYER_FOCUS,
	);
}

static void reimu_dream_bullet_warp(Projectile *p, int t) {
	if(creal(p->args[3]) > 0 /*global.plr.power / 100*/) {
		return;
	}

	double p_long_side = max(creal(p->size), cimag(p->size));
	complex half = 0.5 * (1 + I);
	Rect p_bbox = { p->pos - p_long_side * half, p->pos + p_long_side * half };

	FOR_EACH_GAP(gap) {
		double a = (carg(-gap->pos0) - carg(p->args[0]));

		if(fabs(a) < 2*M_PI/3) {
			continue;
		}

		Rect gap_bbox, overlap;
		complex gap_size = (GAP_LENGTH + I * GAP_WIDTH) * cexp(I*carg(gap->args[0]));
		complex p0 = gap->pos - gap_size * 0.5;
		complex p1 = gap->pos + gap_size * 0.5;
		gap_bbox.top_left = min(creal(p0), creal(p1)) + I * min(cimag(p0), cimag(p1));
		gap_bbox.bottom_right = max(creal(p0), creal(p1)) + I * max(cimag(p0), cimag(p1));

		if(rect_rect_intersection(p_bbox, gap_bbox, true, &overlap)) {
			complex o = (overlap.top_left + overlap.bottom_right) / 2;
			double fract;

			if(creal(gap_size) > cimag(gap_size)) {
				fract = creal(o - gap_bbox.top_left) / creal(gap_size);
			} else {
				fract = cimag(o - gap_bbox.top_left) / cimag(gap_size);
			}

			Enemy *ngap = reimu_dream_gap_get_linked(gap);
			o = ngap->pos + ngap->args[0] * GAP_LENGTH * (1 - fract - 0.5);

			reimu_dream_spawn_warp_effect(gap->pos + gap->args[0] * GAP_LENGTH * (fract - 0.5), false);
			reimu_dream_spawn_warp_effect(o, true);

			p->args[0] = -cabs(p->args[0]) * ngap->pos0;
			p->pos = o + p->args[0];
			p->args[3] += 1;
		}
	}
}

static int reimu_dream_ofuda(Projectile *p, int t) {
	if(t >= 0) {
		reimu_dream_bullet_warp(p, t);
	}

	complex ov = p->args[0];
	double s = cabs(ov);
	p->args[0] *= clamp(s * (1.5 - t / 10.0), s*1.0, 1.5*s) / s;
	int r = reimu_common_ofuda(p, t);
	p->args[0] = ov;

	return r;
}

static int reimu_dream_needle(Projectile *p, int t) {
	if(t >= 0) {
		reimu_dream_bullet_warp(p, t);
	}

	p->angle = carg(p->args[0]);

	if(t < 0) {
		return ACTION_ACK;
	}

	p->pos += p->args[0];

	Color *c = color_mul(COLOR_COPY(&p->color), RGBA_MUL_ALPHA(0.75, 0.5, 1, 0.35));
	c->a = 0;

	PARTICLE(
		.sprite_ptr = p->sprite,
		.color = c,
		.timeout = 12,
		.pos = p->pos,
		.args = { p->args[0] * 0.8, 0, 0+3*I },
		.rule = linear,
		.draw_rule = ScaleFade,
		.layer = LAYER_PARTICLE_LOW,
		.flags = PFLAG_NOREFLECT,
	);

	return ACTION_NONE;
}

static void reimu_dream_shot(Player *p) {
	play_loop("generic_shot");
	int dmg = 50;

	if(!(global.frames % 6)) {
		for(int i = -1; i < 2; i += 2) {
			complex shot_dir = i * ((p->inputflags & INFLAG_FOCUS) ? 1 : I);
			complex spread_dir = shot_dir * cexp(I*M_PI*0.5);

			for(int j = -1; j < 2; j += 2) {
				PROJECTILE(
					.proto = pp_ofuda,
					.pos = p->pos + 10 * j * spread_dir,
					.color = RGBA_MUL_ALPHA(1, 1, 1, 0.5),
					.rule = reimu_dream_ofuda,
					.args = { -20.0 * shot_dir },
					.type = PlrProj,
					.damage = dmg,
					.shader = "sprite_default",
				);
			}
		}
	}
}

static void reimu_dream_slave_visual(Enemy *e, int t, bool render) {
	if(render) {
		r_draw_sprite(&(SpriteParams) {
			.sprite = "yinyang",
			.shader = "sprite_yinyang",
			.pos = {
				creal(e->pos),
				cimag(e->pos),
			},
			.rotation.angle = global.frames * -6 * DEG2RAD,
			.color = RGB(0.95, 0.75, 1.0),
			.scale.both = 0.5,
		});
	}
}

static int reimu_dream_slave(Enemy *e, int t) {
	if(t < 0) {
		return ACTION_ACK;
	}

	// double a = M_PI * psin(t * 0.1) + creal(e->args[0]) + M_PI/2;
	double a = t * -0.1 + creal(e->args[0]) + M_PI/2;
	complex ofs = e->pos0;
	complex shotdir = e->args[1];

	if(global.plr.inputflags & INFLAG_FOCUS) {
		ofs = cimag(ofs) + I * creal(ofs);
		shotdir = cimag(shotdir) + I * creal(shotdir);
	}

	if(t == 0) {
		e->pos = global.plr.pos;
	} else {
		double x = creal(ofs);
		double y = cimag(ofs);
		complex tpos = global.plr.pos + x * sin(a) + y * I * cos(a);
		e->pos += (tpos - e->pos) * 0.5;
	}

	if(player_should_shoot(&global.plr, true)) {
		if(!(global.frames % 6)) {
			PROJECTILE(
				.proto = pp_needle2,
				.pos = e->pos,
				.color = RGBA_MUL_ALPHA(1, 1, 1, 0.35),
				.rule = reimu_dream_needle,
				.args = { 20.0 * shotdir },
				.type = PlrProj,
				.damage = 35,
				.shader = "sprite_default",
			);
		}
	}

	return ACTION_NONE;
}

static Enemy* reimu_dream_spawn_slave(Player *plr, complex pos, complex a0, complex a1, complex a2, complex a3) {
	Enemy *e = create_enemy_p(&plr->slaves, pos, ENEMY_IMMUNE, reimu_dream_slave_visual, reimu_dream_slave, a0, a1, a2, a3);
	e->ent.draw_layer = LAYER_PLAYER_SLAVE;
	return e;
}

static void reimu_dream_kill_slaves(EnemyList *slaves) {
	for(Enemy *e = slaves->first, *next; e; e = next) {
		next = e->next;

		if(e->logic_rule == reimu_dream_slave) {
			delete_enemy(slaves, e);
		}
	}
}

static void reimu_dream_respawn_slaves(Player *plr, short npow) {
	reimu_dream_kill_slaves(&plr->slaves);

	int p = 2 * (npow / 100);
	double s = 1;

	for(int i = 0; i < p; ++i, s = -s) {
		reimu_dream_spawn_slave(plr, 48+32*I, ((double)i/p)*(M_PI*2), s*I, 0, 0);
	}
}

static void reimu_dream_power(Player *p, short npow) {
	if(p->power / 100 != npow / 100) {
		reimu_dream_respawn_slaves(p, npow);
	}
}

static Enemy* reimu_dream_spawn_gap(Player *plr, complex pos, complex a0, complex a1, complex a2, complex a3) {
	Enemy *gap = create_enemy_p(&plr->slaves, pos, ENEMY_IMMUNE, NULL, reimu_dream_gap, a0, a1, a2, a3);
	gap->ent.draw_layer = LAYER_PLAYER_SLAVE;
	return gap;
}

static void reimu_dream_think(Player *plr) {
	if(player_is_bomb_active(plr)) {
		global.shake_view_fade = max(global.shake_view_fade, 5);
	}
}

static void reimu_dream_init(Player *plr) {
	Enemy* left   = reimu_dream_spawn_gap(plr, -1, I, 0, 0, 0);
	Enemy* top    = reimu_dream_spawn_gap(plr, -I, 1, 0, 0, 0);
	Enemy* right  = reimu_dream_spawn_gap(plr,  1, I, 0, 0, 0);
	Enemy* bottom = reimu_dream_spawn_gap(plr,  I, 1, 0, 0, 0);

	reimu_dream_gap_link(top, left);
	reimu_dream_gap_link(bottom, right);

	gap_renderer= create_enemy_p(&plr->slaves, 0, ENEMY_IMMUNE, reimu_dream_gap_renderer_visual, reimu_dream_gap_renderer, 0, 0, 0, 0);
	gap_renderer->ent.draw_layer = LAYER_PLAYER_FOCUS;

	int idx = 0;
	FOR_EACH_GAP(gap) {
		gap->args[3] = creal(gap->args[3]) + I*idx++;
	}

	reimu_dream_respawn_slaves(plr, plr->power);
	reimu_common_bomb_buffer_init();

	size_t sz_vert = sizeof(GenericModelVertex);
	size_t sz_attr = sizeof(GapRenderAttribs);

	#define VERTEX_OFS(attr)   offsetof(GenericModelVertex,  attr)
	#define INSTANCE_OFS(attr) offsetof(GapRenderAttribs, attr)

	VertexAttribFormat fmt[] = {
		// Per-vertex attributes (for the static models buffer, bound at 0)
		{ { 3, VA_FLOAT, VA_CONVERT_FLOAT, 0 }, sz_vert, VERTEX_OFS(position),       0 },
		{ { 3, VA_FLOAT, VA_CONVERT_FLOAT, 0 }, sz_vert, VERTEX_OFS(normal),         0 },
		{ { 2, VA_FLOAT, VA_CONVERT_FLOAT, 0 }, sz_vert, VERTEX_OFS(uv),             0 },

		// Per-instance attributes (for our own buffer, bound at 1)
		{ { 4, VA_FLOAT, VA_CONVERT_FLOAT, 1 }, sz_attr, INSTANCE_OFS(coords),       1 },
		{ { 2, VA_FLOAT, VA_CONVERT_FLOAT, 1 }, sz_attr, INSTANCE_OFS(angles),       1 },
	};

	gap_varray = r_vertex_array_create();
	r_vertex_array_set_debug_label(gap_varray, "ReimuB gaps vertex array");

	gap_vbuffer = r_vertex_buffer_create(NUM_GAPS * sizeof(GapRenderAttribs), NULL);
	r_vertex_buffer_set_debug_label(gap_vbuffer, "ReimuB gaps vertex buffer");

	r_vertex_array_attach_vertex_buffer(gap_varray, r_vertex_buffer_static_models(), 0);
	r_vertex_array_attach_vertex_buffer(gap_varray, gap_vbuffer, 1);
	r_vertex_array_layout(gap_varray, sizeof(fmt)/sizeof(*fmt), fmt);
}

static void reimu_dream_free(Player *plr) {
	r_vertex_array_destroy(gap_varray);
	gap_varray = NULL;

	r_vertex_buffer_destroy(gap_vbuffer);
	gap_vbuffer = NULL;
}

PlayerMode plrmode_reimu_b = {
	.name = "Dream Sign",
	.character = &character_reimu,
	.dialog = &dialog_reimu,
	.shot_mode = PLR_SHOT_REIMU_DREAM,
	.procs = {
		.property = reimu_common_property,
		.bomb = reimu_dream_bomb,
		.bombbg = reimu_dream_bomb_bg,
		.shot = reimu_dream_shot,
		.power = reimu_dream_power,
		.init = reimu_dream_init,
		.free = reimu_dream_free,
		.preload = reimu_dream_preload,
		.think = reimu_dream_think,
	},
};
