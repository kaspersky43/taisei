/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2019, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2019, Andrei Alexeyev <akari@alienslab.net>.
 */

#ifndef IGUARD_renderer_glcommon_vtable_h
#define IGUARD_renderer_glcommon_vtable_h

#include "taisei.h"

#include "../api.h"
#include "../common/backend.h"
#include "texture.h"

typedef struct GLVTable {
	GLTextureTypeInfo* (*texture_type_info)(TextureType type);
	void (*init_context)(SDL_Window *window);
} GLVTable;

typedef struct GLBackendData {
	GLVTable vtable;
} GLBackendData;

#define GLVT_OF(backend) (((GLBackendData*)backend.custom)->vtable)
#define GLVT GLVT_OF(_r_backend)

#endif // IGUARD_renderer_glcommon_vtable_h
