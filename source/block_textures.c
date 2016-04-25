#include "block_textures.h"
blockTexture blocktextures[16][16];

void initTextures() {
	int x, y;
	for (x = 0; x<16; x++) {
		for (y = 0; y<16; y++) {
			blocktextures[x][y].u0 = ((f32)x)*(1.0f/16.0f);
			blocktextures[x][y].v0 = ((f32)y)*(1.0f/16.0f);
			// TODO: Textures on the edge of the sprite sheet need the 0.0001 hack.
			blocktextures[x][y].u1 = blocktextures[x][y].u0+(1.0f/16.0f)-(x<15?0:0.0001);
			blocktextures[x][y].v1 = blocktextures[x][y].v0+(1.0f/16.0f)-(y<15?0:0.0001);
		}
	}
}

blockTexture *getTexture(int x, int y) {
	return &(blocktextures[x][y]);
}
