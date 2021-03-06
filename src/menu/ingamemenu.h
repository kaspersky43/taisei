/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2019, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2019, Andrei Alexeyev <akari@alienslab.net>.
 */

#ifndef IGUARD_menu_ingamemenu_h
#define IGUARD_menu_ingamemenu_h

#include "taisei.h"

#include "menu.h"

void draw_ingame_menu_bg(MenuData *menu, float f);
void draw_ingame_menu(MenuData *menu);

void create_ingame_menu(MenuData *menu);
void create_ingame_menu_replay(MenuData *m);

void update_ingame_menu(MenuData *menu);

void restart_game(MenuData *m, void *arg);

#endif // IGUARD_menu_ingamemenu_h
