/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2019, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2019, Andrei Alexeyev <akari@alienslab.net>.
 */

#ifndef IGUARD_vfs_zipfile_impl_h
#define IGUARD_vfs_zipfile_impl_h

#include "taisei.h"

#include <zip.h>
#include "private.h"
#include "hashtable.h"

/* zipfile */

typedef struct VFSZipFileTLS {
	zip_t *zip;
	SDL_RWops *stream;
	zip_error_t error;
} VFSZipFileTLS;

typedef struct VFSZipFileData {
	VFSNode *source;
	ht_str2int_t pathmap;
	SDL_TLSID tls_id;
} VFSZipFileData;

typedef struct VFSZipFileIterData {
	zip_int64_t idx;
	zip_int64_t num;
	const char *prefix;
	size_t prefix_len;
	char *allocated;
} VFSZipFileIterData;

const char* vfs_zipfile_iter_shared(VFSNode *node, VFSZipFileData *zdata, VFSZipFileIterData *idata, VFSZipFileTLS *tls);
void vfs_zipfile_iter_stop(VFSNode *node, void **opaque);

/* zippath */

typedef struct VFSZipPathData {
	VFSNode *zipnode;
	VFSZipFileTLS *tls;
	uint64_t index;
	VFSInfo info;
} VFSZipPathData;

void vfs_zippath_init(VFSNode *node, VFSNode *zipnode, VFSZipFileTLS *tls, zip_int64_t idx);

#endif // IGUARD_vfs_zipfile_impl_h
