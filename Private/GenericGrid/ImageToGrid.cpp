// Copyright Gradientspace Corp. All Rights Reserved.
#include "GenericGrid/ImageToGrid.h"

using namespace GS;

void ImageToGrid::SetGrid(UniformGridAdapter* AdapterIn)
{
	Adapter = AdapterIn;
}

bool ImageToGrid::Rasterize(
	const Vector2i& ImageCoordMin,
	const Vector2i& ImageCoordMax,
	const Vector3i& GridMinCoord,
	const Vector2i& AxisMapping,
	FunctionRef<bool(const Vector2i& PixelCoord, Color4b& PixelColorOut)> ImageSampleFunc)
{
	bool bFilledAnyCells = false;
	AxisBox3i ModifiedBounds = AxisBox3i::Empty();

	int ImageWidth = ImageCoordMax.X - ImageCoordMin.X + 1;
	int ImageHeight = ImageCoordMax.Y - ImageCoordMin.Y + 1;

	bool bFlipX = AxisMapping.X < 0;
	bool bFlipY = AxisMapping.Y < 0;
	int MapX = GS::Clamp(GS::Abs(AxisMapping[0]), 0, 2);
	int MapY = GS::Clamp(GS::Abs(AxisMapping[1]), 0, 2);
	if ( MapX == MapY ) MapY = (MapX+1)%3;

	// todo parallel
	for (int yi = ImageCoordMin.Y; yi <= ImageCoordMax.Y; yi++)
	{
		for (int xi = ImageCoordMin.X; xi <= ImageCoordMax.X; xi++)
		{
			Vector3i CellIndex(GridMinCoord);
			CellIndex[MapX] += (bFlipX) ? (ImageWidth-xi) : xi;
			CellIndex[MapY] += (bFlipY) ? (ImageHeight-yi) : yi;
			if (Adapter->IsValidIndex(CellIndex) == false) continue;

			Color4b PixelColor = Color4b::White();;
			bool bIsPixel = ImageSampleFunc(Vector2i(xi,yi), PixelColor);
			if (bIsPixel && PixelColor.A > 0)
			{
				GenericGridCellState CellState;
				if ( Adapter->GetCellState(CellIndex, CellState) )
				{
					CellState.bFilled = true;
					CellState.Color = Color3b(PixelColor.R, PixelColor.G, PixelColor.B);
					bool bModified = false;
					if (Adapter->SetCellState(CellIndex, CellState, bModified))
					{
						if (bModified)
						{
							bFilledAnyCells = true;
							ModifiedBounds.Contain(CellIndex);
						}
					}
				}
			}
		}
	}

	ModifiedCellIndexBounds = ModifiedBounds;

	return bFilledAnyCells;
}
