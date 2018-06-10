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

// FIXME: We probably need a better way to store shot-specific state.
//        See also MarisaA.
static struct {
	uint prev_inputflags;
	bool respawn_slaves;
} reimu_spirit_state;

static void reimu_spirit_preload(void) {
	const int flags = RESF_DEFAULT;

	preload_resources(RES_SPRITE, flags,
		"yinyang",
		"proj/ofuda",
		"proj/needle",
		"part/ofuda_glow",
	NULL);

	preload_resources(RES_SHADER_PROGRAM, flags,
		"sprite_yinyang",
	NULL);

	//preload_resources(RES_SFX, flags | RESF_OPTIONAL,
	//NULL);
}

static int reimu_spirit_needle(Projectile *p, int t) {
	int r = linear(p, t);

	if(t < 0) {
		return r;
	}

	PARTICLE(
		.sprite_ptr = p->sprite,
		.color = multiply_colors(p->color, rgba(0.75, 0.5, 1, 0.5)),
		.timeout = 12,
		.pos = p->pos,
		.args = { p->args[0] * 0.8, 0, 0+3*I },
		.rule = linear,
		.draw_rule = ScaleFade,
		.layer = LAYER_PARTICLE_LOW,
		.flags = PFLAG_NOREFLECT,
		.blend = BLEND_ADD,
	);

	return r;
}

#define REIMU_SPIRIT_HOMING_SCALE 0.75

static void reimu_spirit_homing_draw(Projectile *p, int t) {
	r_mat_push();
	r_mat_translate(creal(p->pos), cimag(p->pos), 0);
	r_mat_rotate(p->angle + M_PI/2, 0, 0, 1);
	r_mat_scale(REIMU_SPIRIT_HOMING_SCALE, REIMU_SPIRIT_HOMING_SCALE, 1);
	ProjDrawCore(p, p->color);
	r_mat_pop();
}

static Projectile* reimu_spirit_spawn_ofuda_particle(Projectile *p, int t) {
	return PARTICLE(
		.sprite = "ofuda_glow",
		// .color = rgba(0.5 + 0.5 + psin(global.frames * 0.75), psin(t*0.5), 1, 0.5),
		.color = hsla(t * 0.1, 0.5, 0.75, 0.35),
		.timeout = 12,
		.pos = p->pos,
		.args = { p->args[0] * (0.2 + 0.6 * frand()), 0, (1+2*I) * REIMU_SPIRIT_HOMING_SCALE },
		.angle = p->angle,
		.rule = linear,
		.draw_rule = ScaleFade,
		.layer = LAYER_PARTICLE_LOW,
		.flags = PFLAG_NOREFLECT,
		.blend = BLEND_ADD,
	);
}

static int reimu_spirit_homing_impact(Projectile *p, int t) {
	if(t < 0) {
		return ACTION_ACK;
	}

	Projectile *trail = reimu_spirit_spawn_ofuda_particle(p, global.frames);
	trail->rule = NULL;
	trail->timeout = 6;
	trail->angle = p->angle;
	trail->ent.draw_layer = LAYER_PLAYER_FOCUS - 1; // TODO: add a layer for "super high" particles?
	trail->args[2] *= 1.5 * (1 - t/p->timeout);
	p->angle += 0.2;

	return ACTION_NONE;
}

static Projectile* reimu_spirit_spawn_homing_impact(Projectile *p, int t) {
	return PARTICLE(
		.proto = p->proto,
		.color = p->color,
		.timeout = 24,
		.pos = p->pos,
		.args = { 0, 0, (1+2*I) * REIMU_SPIRIT_HOMING_SCALE },
		.angle = p->angle,
		.rule = reimu_spirit_homing_impact,
		.draw_rule = ScaleFade,
		.layer = LAYER_PARTICLE_HIGH,
		.flags = PFLAG_NOREFLECT,
	);
}

static int reimu_spirit_homing(Projectile *p, int t) {
	if(t < 0) {
		if(t == EVENT_DEATH && projectile_in_viewport(p)) {
			reimu_spirit_spawn_homing_impact(p, t);
		}

		return ACTION_ACK;
	}

	p->args[3] = plrutil_homing_target(p->pos, p->args[3]);
	double v = cabs(p->args[0]);
	complex aimdir = cexp(I*carg(p->args[3] - p->pos));

	double aim = (0.5 * pow(1 - t / p->timeout, 4));

	p->args[0] += v * aim * aimdir;
	p->args[0] = v * cexp(I*carg(p->args[0]));
	p->angle = carg(p->args[0]);
	p->pos += p->args[0];

	reimu_spirit_spawn_ofuda_particle(p, t);
	return ACTION_NONE;
}

static void reimu_spirit_bomb(Player *p) {
}

static void reimu_spirit_shot(Player *p) {
	reimu_common_shot(p, 100 - 17 * (p->power / 100));
}

static void reimu_spirit_slave_shot(Enemy *e, int t) {
	// int st = global.frames + 2;
	int st = t;

	if(st % 3) {
		return;
	}

	if(global.plr.inputflags & INFLAG_FOCUS) {
		PROJECTILE(
			.proto = pp_needle,
			.pos = e->pos - 25.0*I,
			.color = rgba(1, 1, 1, 0.5),
			.rule = reimu_spirit_needle,
			.args = { -20.0*I },
			.type = PlrProj + cimag(e->args[2]),
			.shader = "sprite_default",
		);
	} else if(!(st % 6)) {
		PROJECTILE(
			.proto = pp_ofuda,
			.pos = e->pos,
			.color = rgba(1, 1, 1, 0.5),
			.rule = reimu_spirit_homing,
			.draw_rule = reimu_spirit_homing_draw,
			.args = { -10.0*I, 0, 0, creal(e->pos) },
			.type = PlrProj + creal(e->args[2]),
			.timeout = 60,
			.shader = "sprite_default",
			.flags = PFLAG_NOCOLLISIONEFFECT,
		);
	}
}

static int reimu_spirit_slave(Enemy *e, int t) {
	TIMER(&t);

	AT(EVENT_BIRTH) {
		e->pos = global.plr.pos;
		return ACTION_NONE;
	}

	if(t < 0) {
		return ACTION_NONE;
	}

	if(player_should_shoot(&global.plr, true)) {
		reimu_spirit_slave_shot(e, t);
	}

	if(creal(e->args[3]) != 0) {
		int death_begin_time = creal(e->args[3]);
		int death_duration = cimag(e->args[3]);
		double death_progress = (t - death_begin_time) / (double)death_duration;

		e->pos = global.plr.pos * death_progress + e->pos0 * (1 - death_progress);

		if(death_progress >= 1) {
			return ACTION_DESTROY;
		}

		return ACTION_NONE;
	}

	double speed = 0.005 * min(1, t / 12.0);

	if(global.plr.inputflags & INFLAG_FOCUS) {
		GO_TO(e, global.plr.pos + cimag(e->args[1]) * cexp(I*(e->args[0] + t * creal(e->args[1]))), speed * cabs(e->args[1]));
	} else {
		GO_TO(e, global.plr.pos + e->pos0, speed * cabs(e->pos0));
	}

	return ACTION_NONE;
}

static int reimu_spirit_yinyang_flare(Projectile *p, int t) {
	double a = p->angle;
	int r = linear(p, t);
	p->angle = a;
	return r;
}

static void reimu_spirit_yinyang_focused_visual(Enemy *e, int t, bool render) {
	if(!render && player_should_shoot(&global.plr, true)) {
		PARTICLE(
			.sprite = "stain",
			.color = rgba(1, 0.0 + 0.5 * frand(), 0, 1),
			.timeout = 12 + 2 * nfrand(),
			.pos = e->pos,
			.args = { -5*I * (1 + frand()), 0, 0.5 + 0*I },
			.angle = 2*M_PI*frand(),
			.rule = reimu_spirit_yinyang_flare,
			.draw_rule = ScaleFade,
			.layer = LAYER_PARTICLE_HIGH,
			.flags = PFLAG_NOREFLECT,
			.blend = BLEND_ADD,
		);
	}

	reimu_common_draw_yinyang(e, t, rgb(1.0, 0.8, 0.8));
}

static void reimu_spirit_yinyang_unfocused_visual(Enemy *e, int t, bool render) {
	if(!render && player_should_shoot(&global.plr, true)) {
		PARTICLE(
			.sprite = "stain",
			.color = rgba(1, 0.25, 0.0 + 0.5 * frand(), 1),
			.timeout = 12 + 2 * nfrand(),
			.pos = e->pos,
			.args = { -5*I * (1 + frand()), 0, 0.5 + 0*I },
			.angle = 2*M_PI*frand(),
			.rule = reimu_spirit_yinyang_flare,
			.draw_rule = ScaleFade,
			.layer = LAYER_PARTICLE_HIGH,
			.flags = PFLAG_NOREFLECT,
			.blend = BLEND_ADD,
		);
	}

	reimu_common_draw_yinyang(e, t, rgb(0.95, 0.75, 1.0));
}

static inline Enemy* reimu_spirit_spawn_slave(Player *plr, complex pos, complex a0, complex a1, complex a2, complex a3, EnemyVisualRule visual) {
	Enemy *e = create_enemy_p(&plr->slaves, pos, ENEMY_IMMUNE, visual, reimu_spirit_slave, a0, a1, a2, a3);
	e->ent.draw_layer = LAYER_PLAYER_SLAVE;
	return e;
}

static void reimu_spirit_kill_slaves(EnemyList *slaves) {
	for(Enemy *e = slaves->first, *next; e; e = next) {
		next = e->next;

		if(e->hp == ENEMY_IMMUNE && creal(e->args[3]) == 0) {
			// delete_enemy(slaves, e);
			// e->args[3] = 1;
			e->args[3] = global.frames - e->birthtime + 3 * I;
			e->pos0 = e->pos;
		}
	}
}

static void reimu_spirit_respawn_slaves(Player *plr, short npow, complex param) {
	double dmg_homing = 60; // every 6 frames
	double dmg_needle = 80; // every 3 frames
	complex dmg = dmg_homing + I * dmg_needle;
	EnemyVisualRule visual;

	if(plr->inputflags & INFLAG_FOCUS) {
		visual = reimu_spirit_yinyang_focused_visual;
	} else {
		visual = reimu_spirit_yinyang_unfocused_visual;
	}

	reimu_spirit_kill_slaves(&plr->slaves);

	switch(npow / 100) {
		case 0:
			break;
		case 1:
			reimu_spirit_spawn_slave(plr, 50.0*I, 0,          +0.10 + 60*I, dmg, 0, visual);
			break;
		case 2:
			reimu_spirit_spawn_slave(plr, +40,    0,          +0.10 + 60*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, -40,    M_PI,       +0.10 + 60*I, dmg, 0, visual);
			break;
		case 3:
			reimu_spirit_spawn_slave(plr, 50.0*I, 0*2*M_PI/3, +0.10 + 60*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, +40,    1*2*M_PI/3, +0.10 + 60*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, -40,    2*2*M_PI/3, +0.10 + 60*I, dmg, 0, visual);
			break;
		case 4:
			reimu_spirit_spawn_slave(plr, +80,    0,          +0.10 + 60*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, -80,    M_PI,       +0.10 + 60*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, +40,    0,          -0.05 + 80*I, dmg, 0, visual);
			reimu_spirit_spawn_slave(plr, -40,    M_PI,       -0.05 + 80*I, dmg, 0, visual);
			break;
		default:
			UNREACHABLE;
	}
}

static void reimu_spirit_init(Player *plr) {
	memset(&reimu_spirit_state, 0, sizeof(reimu_spirit_state));
	reimu_spirit_state.prev_inputflags = plr->inputflags;
	reimu_spirit_respawn_slaves(plr, plr->power, 0);
}

static void reimu_spirit_think(Player *plr) {
	if((bool)(reimu_spirit_state.prev_inputflags & INFLAG_FOCUS) ^ (bool)(plr->inputflags & INFLAG_FOCUS)) {
		reimu_spirit_state.respawn_slaves = true;
	}

	if(reimu_spirit_state.respawn_slaves) {
		if(plr->slaves.first) {
			reimu_spirit_kill_slaves(&plr->slaves);
		} else {
			reimu_spirit_respawn_slaves(plr, plr->power, 0);
			reimu_spirit_state.respawn_slaves = false;
		}
	}

	reimu_spirit_state.prev_inputflags = plr->inputflags;
}

static void reimu_spirit_power(Player *plr, short npow) {
	if(plr->power / 100 != npow / 100) {
		reimu_spirit_respawn_slaves(plr, npow, 0);
	}
}

PlayerMode plrmode_reimu_a = {
	.name = "Spirit Sign",
	.character = &character_reimu,
	.dialog = &dialog_reimu,
	.shot_mode = PLR_SHOT_REIMU_SPIRIT,
	.procs = {
		.property = reimu_common_property,
		.init = reimu_spirit_init,
		.think = reimu_spirit_think,
		.bomb = reimu_spirit_bomb,
		.shot = reimu_spirit_shot,
		.power = reimu_spirit_power,
		.preload = reimu_spirit_preload,
	},
};