
#pragma once

#include "BlockHandler.h"
#include "../World.h"





class cBlockIceHandler :
	public cBlockHandler
{
public:
	cBlockIceHandler(BLOCKTYPE a_BlockType)
		: cBlockHandler(a_BlockType)
	{
	}


	virtual void ConvertToPickups(cItems & a_Pickups, NIBBLETYPE a_BlockMeta) override
	{
		// No pickups
	}
	
	
	virtual void OnDestroyedByPlayer(cChunkInterface & a_ChunkInterface, cWorldInterface & a_WorldInterface, cPlayer * a_Player, int a_BlockX, int a_BlockY, int a_BlockZ) override
	{
		if (a_Player->IsGameModeCreative() || (a_BlockY <= 0))
		{
			return;
		}

		BLOCKTYPE BlockBelow = a_ChunkInterface.GetBlock(a_BlockX, a_BlockY - 1, a_BlockZ);
		if (!cBlockInfo::FullyOccupiesVoxel(BlockBelow) && !IsBlockLiquid(BlockBelow))
		{
			return;
		}

		a_ChunkInterface.FastSetBlock(a_BlockX, a_BlockY, a_BlockZ, E_BLOCK_WATER, 0);
		// This is called later than the real destroying of this ice block
	}
} ;



