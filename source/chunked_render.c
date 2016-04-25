#include <grrlib.h>
#include <math.h>
#include <stdlib.h>

#include "chunked_render.h"
#include "netcat_logger.h"
#include "block.h"
#include "render.h"
#include "fail3d.h"
#include "main.h"

#define chunked_isoob(rc,rcd,px,pz) (rc->x > px+rcd) || (rc->x < px-rcd) || (rc->z > pz+rcd) || (rc->z < pz-rcd)

renderchunk *renderchunks[nRenderChunks];
int renderorder[nRenderChunks];

void chunked_init() {
	//initialize all the renderchunks
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = malloc(sizeof(renderchunk));
		rc->active = false;
		rc->update = false;
		rc->list = NULL;
		renderchunks[i] = rc;
	}
}

void chunked_deallocall() {
	int i;
	//dealloc all the renderchunks
	for (i=0; i<nRenderChunks; i++) {
		renderchunks[i]->active = false;
	}
}

int chunked_getchunkfromchunkpos(unsigned short x, unsigned short z) {
	//first try to find an existing renderchunk with the coords
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active && rc->x == x && rc->z == z) {
			netcat_log("found active chunk with same x and z\n");
			return i;
		}
	}
	//else, find an inactive chunk
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (!rc->active) {
			netcat_log("found inactive chunk\n");
			rc->x = x;
			rc->z = z;
			return i;
		}
	}
	netcat_log("no more chunks!\n");
	return -1;
}

void chunked_markchunkforupdate(unsigned short x, unsigned short z) {
	//try to find an existing renderchunk with the coords
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active && rc->x == x && rc->z == z) {
			netcat_log("marking active chunk for render update\n");
			rc->update = true;
			return;
		}
	}
	netcat_log("no chunk to mark for render update!\n");
}

void chunked_rerenderChunkUpdates() {
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active && rc->update) {
			chunked_rerenderChunk(rc->x, rc->z, true);
		}
	}
}

inline void chunked_rerenderChunk(signed short cx, signed short cz, bool force) {
	if (cx < 0 || cz < 0 || cx >= worldX/chunkSize || cz >= worldZ/chunkSize)
		return;
	renderchunk *rc = renderchunks[chunked_getchunkfromchunkpos(cx,cz)];
	if ((!rc->active) || rc->update || force) {
		rc->active = true;
		rc->update = false;
		netcat_logf("rendering chunk %d, %d\n",cx,cz);
		//check for display list
		if (rc->list == NULL) {
			rc->list = displist_create(256);
			rc->blendlist = displist_create(256);
		}
		//start rendering blocks
		int bx, bz;
		bx = cx*chunkSize;
		bz = cz*chunkSize;

		displist_clear(rc->list);
		displist_bind(rc->list);
		int x;
		int y;
		int z;
		for (y = worldY - 1; y >= 0; y--) {
			for (x = bx; x < bx+chunkSize; x++) {
				for (z = bz; z < bz+chunkSize; z++) {
					u8 blockID = theWorld[y][x][z];
					if (blockID != 0) {
						blockEntry entry = blockRegistry[blockID];
						entry.renderBlock(x, y, z, 0);
					}
				}
			}
		}
		displist_fit(rc->list);
		netcat_log("rendered pass 0\n");

		displist_clear(rc->blendlist);
		displist_bind(rc->blendlist);
		for (y = 0; y < worldY; y++) {
			for (x = bx; x < bx+chunkSize; x++) {
				for (z = bz; z < bz+chunkSize; z++) {
					u8 blockID = theWorld[y][x][z];
					if (blockID != 0) {
						blockEntry entry = blockRegistry[blockID];
						entry.renderBlock(x, y, z, 1);
					}
				}
			}
		}
		displist_fit(rc->blendlist);
		netcat_log("rendered pass 1\n");
		netcat_logf("%d/%d\n",rc->list->index,rc->list->size);
	}
	else
	{
		netcat_logf("chunk %d, %d already rendered\n",cx,cz);
	}
}

void chunked_refresh(int renderDistance, player thePlayer) {
	//convert the player's position to chunk position
	signed short px, pz;
	px = thePlayer.posX/chunkSize;
	pz = thePlayer.posZ/chunkSize;
	int rcd = renderDistance/chunkSize;
	//remove the old chunks that are now out of range
	int nactive = 0;
	int nremoved = 0;
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active) {
			if (chunked_isoob(rc, rcd, px, pz)) {
				rc->active = false;
				rc->update = false;
				nremoved++;
			}
			else
			{
				nactive++;
			}
		}
	}
	//if (nactive>0 && nremoved == 0) return; //could be problematic, attempts to stop if no chunks got removed
	//start rendering chunks
	unsigned short cx, cz;
	unsigned short maxcx, maxcz;
	maxcx = worldX/chunkSize;
	maxcz = worldZ/chunkSize;

	for (cx = max(px-rcd,0); cx < min(maxcx,px + rcd); cx++) {
		for (cz = max(pz-rcd,0); cz < min(maxcz,pz + rcd); cz++) {
			chunked_rerenderChunk(cx,cz,false);
		}
	}
}

static player cmp_player;
int chunk_cmp(const void *a, const void *b) {
	renderchunk *ca = renderchunks[*((const int *)a)];
	renderchunk *cb = renderchunks[*((const int *)b)];
	int adst = abs(ca->x*chunkSize - cmp_player.posX) + abs(ca->z*chunkSize - cmp_player.posZ);
	int bdst = abs(cb->x*chunkSize - cmp_player.posX) + abs(cb->z*chunkSize - cmp_player.posZ);
	if (adst > bdst)
		return -1;
	else if (adst < bdst)
		return 1;
	else
		return 0;
}

void calculateChunkPoint(guVector *polygon, guVector *center, guVector *camera) {
	fail3d_translatePoint(polygon, center);
	fail3d_rotatePoint(polygon, camera);
	fail3d_calculatePointPosition(polygon);
}

void chunked_render(player thePlayer) {
	guVector center = {thePlayer.posX, -thePlayer.posY - 1.625, -thePlayer.posZ};
	guVector camera = {thePlayer.lookY, thePlayer.lookX, thePlayer.lookZ};
	guVector polygon;

	displist_start();
	int i;
	int nrendered = 0;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active) {
			bool allL, allR, allU, allD;
			allL = allR = allU = allD = true;
			bool vZ = false;

#define testChunk(xval, yval, zval) \
polygon.x = xval; \
polygon.y = -(yval); \
polygon.z = -(zval); \
calculateChunkPoint(&polygon, &center, &camera); \
if (polygon.x >= 0) allL = false; \
if (polygon.x <= rmode->fbWidth) allR = false; \
if (polygon.y >= 0) allU = false; \
if (polygon.y <= rmode->efbHeight) allD = false; \
if (polygon.z > 0) vZ = true;
			testChunk(rc->x*chunkSize,           0,      rc->z*chunkSize);
			testChunk(rc->x*chunkSize,           worldY, rc->z*chunkSize);
			testChunk(rc->x*chunkSize+chunkSize, 0,      rc->z*chunkSize);
			testChunk(rc->x*chunkSize+chunkSize, worldY, rc->z*chunkSize);
			testChunk(rc->x*chunkSize,           0,      rc->z*chunkSize+chunkSize);
			testChunk(rc->x*chunkSize,           worldY, rc->z*chunkSize+chunkSize);
			testChunk(rc->x*chunkSize+chunkSize, 0,      rc->z*chunkSize+chunkSize);
			testChunk(rc->x*chunkSize+chunkSize, worldY, rc->z*chunkSize+chunkSize);
#undef testChunk
			if (!(allL || allR || allU || allD) && vZ) {
				renderorder[nrendered] = i;
				nrendered++;
			}
		}
	}
	//sort the render order
	cmp_player = thePlayer;
	qsort(renderorder, nrendered, sizeof(int), chunk_cmp);
	for (i=0; i<nrendered; i++) {
		renderchunk *rc = renderchunks[renderorder[i]];
		displist_render(rc->list);
	}
	/*GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_RASC,GX_CC_ONE,GX_CC_TEXC,GX_CC_ZERO);
	GX_SetTevAlphaIn(GX_TEVSTAGE0,GX_CA_TEXA,GX_CA_RASA,GX_CA_TEXA,GX_CC_RASA);
	GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
	GX_SetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);*/
	for (i=0; i<nrendered; i++) {
		renderchunk *rc = renderchunks[renderorder[i]];
		displist_render(rc->blendlist);
	}
}

int chunked_getfifousage() {
	int usage = 0;
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active) {
			usage += rc->list->index-1;
			usage += rc->blendlist->index-1;
		}
	}
	return usage;
}

int chunked_getfifototal() {
	int usage = 0;
	int i;
	for (i=0; i<nRenderChunks; i++) {
		renderchunk *rc = renderchunks[i];
		if (rc->active) {
			usage += rc->list->size;
			usage += rc->blendlist->size;
		}
	}
	return usage;
}
