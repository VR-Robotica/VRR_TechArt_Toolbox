// Copyright VR Robotica. All Rights Reserved.

/*
 * UV layout: a new, non-overlapping UV channel built from the islands of an existing one.
 *
 * Process (BuildLayout, per LOD)
 *  1. Find the islands: triangles are joined where they share a corner with the same position and the same
 *     source UV (so UV seams split islands, hard normals don't). Mirrored or stacked islands stay separate,
 *     which is what makes the result unique.
 *  2. Scale every island by sqrt(3D area / UV area), so all islands get the same texel density.
 *     Islands with no UV area are laid flat from their 3D shape instead.
 *  3. Optionally rotate each island to its tightest bounding box (tested on its convex hull): lying flat,
 *     or standing up when its region is a column (taller than wide).
 *  4. Group islands by material slot, numbered in slot order (the Vrr UV Region material node uses these numbers).
 *     Each group gets a region of the 0-1 square: 1 slot the whole square,
 *     2 two columns, 3 three columns; more fill extra rows (4 = 2 + 2, 5 = 3 + 2), so no space is left empty.
 *     As UDIMs, each group gets a whole tile instead: 1001, 1002, ... 1010, then 1011 one unit down in V.
 *  5. Pack each group into its region, with Padding pixels between islands, at the largest scale that fits.
 *     Boxes: bounding boxes on shelves (best of several shelf widths). Shapes: each island drawn onto a grid
 *     and placed largest first where its top ends lowest, trying all four quarter turns, so islands nest into
 *     each other's gaps and holes. With matched density every group uses the smallest scale.
 *     Stretch to fill then scales each axis so the islands reach the region's edges.
 *
 * Apply
 *  Writes the result into the target channel. If a static mesh's lightmap uses that channel (its lightmap index,
 *  or Generate Lightmap UVs writing there), a channel is inserted at the target first, so the lightmap moves up
 *  one channel and the indices follow it.
 *
 * Balanced UV layout (ApplyBalanced)
 *  The same layout with one region for every island and its own padding. Written into its own target channel
 *  (Apply), or on static meshes optionally as the lightmap UVs (ApplyLightmap): written where Unreal's generated
 *  lightmap went (Generate Lightmap UVs' destination, else the lightmap index, else a new last channel).
 *  Generate Lightmap UVs is turned off so the build keeps them, and the lightmap index and resolution point at them.
 *
 * Texel density (ApplyTexelDensity)
 *  Rescales an existing channel in place (usually UV0, for tiling textures) so one UV tile covers a set distance,
 *  e.g. a 512 px texture over 0.5 m = 1024 px/m. Nothing is moved or packed: islands keep their place and shape.
 *  1. Find the channel's islands (as in step 1 above, across material slots).
 *  2. Measure each island: density = sqrt(UV area / 3D area), in UV units per cm.
 *  3. Scale by target / current: each island about its own center (even density everywhere), or the whole
 *     channel by its average factor about its center (keeps the relative sizes the artist chose).
 *  Islands with no UV area can't be measured and are left as they are. The lightmap channel is refused (it must
 *  stay inside 0-1).
 */

#include "Core_UVLayout.h"

#include "Async/ParallelFor.h"
#include "Components.h"
#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "MeshUVChannelInfo.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshTypes.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

#define LOCTEXT_NAMESPACE "UVLayout"

namespace UVLayoutImpl
{
	/** A triangle corner as far as islands are concerned: position, source UV (quantized) and group. */
	struct FCornerKey
	{
		int32 Vertex;
		int32 U;
		int32 V;
		int32 Group;

		bool operator==(const FCornerKey& Other) const
		{
			return Vertex == Other.Vertex && U == Other.U && V == Other.V && Group == Other.Group;
		}
		friend uint32 GetTypeHash(const FCornerKey& Key)
		{
			return HashCombineFast(HashCombineFast(::GetTypeHash(Key.Vertex), ::GetTypeHash(Key.U)), HashCombineFast(::GetTypeHash(Key.V), ::GetTypeHash(Key.Group)));
		}
	};

	/** Disjoint sets over triangle indices. */
	struct FUnionFind
	{
		TArray<int32> Parent;

		explicit FUnionFind(int32 Num)
		{
			Parent.SetNumUninitialized(Num);
			for (int32 i = 0; i < Num; ++i)
			{
				Parent[i] = i;
			}
		}
		int32 Find(int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};

	struct FIsland
	{
		int32 Group = 0;
		TArray<int32> Triangles;
		/** Vertex instances (array indices) and their layout position, in cm (same density for every island). */
		TArray<int32> Instances;
		TArray<FVector2f> Points;
		/** Each triangle's corners as indices into Points (3 per triangle), for shape packing. */
		TArray<int32> Corners;
		/** Surface area in layout units (cm^2). */
		float Area = 0.0f;
		FVector2f Size = FVector2f::ZeroVector;
		bool bProjected = false;
	};

	/** The finished layout of one LOD. */
	struct FLayout
	{
		/** New UV per vertex instance (indexed by array index; unused instances stay 0). */
		TArray<FVector2f> UVs;
		FUVLayoutPreview Preview;
	};

	float Cross2D(const FVector2f& A, const FVector2f& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Convex hull (Andrew's monotone chain). The rotation search only needs the hull, not every point. */
	TArray<FVector2f> ConvexHull(TArray<FVector2f> Points)
	{
		if (Points.Num() < 3)
		{
			return Points;
		}
		Points.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		TArray<FVector2f> Hull;
		Hull.SetNumUninitialized(Points.Num() * 2);
		int32 Count = 0;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			while (Count >= 2 && Cross2D(Hull[Count - 1] - Hull[Count - 2], Points[i] - Hull[Count - 2]) <= 0.0f)
			{
				--Count;
			}
			Hull[Count++] = Points[i];
		}
		for (int32 i = Points.Num() - 2, Lower = Count + 1; i >= 0; --i)
		{
			while (Count >= Lower && Cross2D(Hull[Count - 1] - Hull[Count - 2], Points[i] - Hull[Count - 2]) <= 0.0f)
			{
				--Count;
			}
			Hull[Count++] = Points[i];
		}
		Hull.SetNum(FMath::Max(Count - 1, 1));
		return Hull;
	}

	/** Bounding box size of the points rotated by Angle (radians). */
	FVector2f RotatedExtent(const TArray<FVector2f>& Points, float Angle)
	{
		const float Cos = FMath::Cos(Angle);
		const float Sin = FMath::Sin(Angle);
		FVector2f Min(UE_BIG_NUMBER), Max(-UE_BIG_NUMBER);
		for (const FVector2f& P : Points)
		{
			const FVector2f R(P.X * Cos - P.Y * Sin, P.X * Sin + P.Y * Cos);
			Min = FVector2f::Min(Min, R);
			Max = FVector2f::Max(Max, R);
		}
		return Max - Min;
	}

	/** Turns the island to its smallest bounding box, wider than tall, and moves its corner to the origin. */
	void OrientIsland(FIsland& Island, bool bRotate)
	{
		if (bRotate && Island.Points.Num() >= 3)
		{
			const TArray<FVector2f> Hull = ConvexHull(Island.Points);
			// Coarse search in 1 degree steps, then refine around the best
			float BestAngle = 0.0f;
			float BestArea = UE_BIG_NUMBER;
			auto Try = [&](float Angle)
			{
				const FVector2f Extent = RotatedExtent(Hull, Angle);
				if (Extent.X * Extent.Y < BestArea)
				{
					BestArea = Extent.X * Extent.Y;
					BestAngle = Angle;
				}
			};
			for (int32 Degree = 0; Degree < 90; ++Degree)
			{
				Try(FMath::DegreesToRadians(static_cast<float>(Degree)));
			}
			const float Coarse = BestAngle;
			for (int32 Step = -10; Step <= 10; ++Step)
			{
				Try(Coarse + FMath::DegreesToRadians(Step * 0.1f));
			}

			// Lying flat packs better on shelves
			const FVector2f Extent = RotatedExtent(Hull, BestAngle);
			if (Extent.Y > Extent.X)
			{
				BestAngle += UE_HALF_PI;
			}
			const float Cos = FMath::Cos(BestAngle);
			const float Sin = FMath::Sin(BestAngle);
			for (FVector2f& P : Island.Points)
			{
				P = FVector2f(P.X * Cos - P.Y * Sin, P.X * Sin + P.Y * Cos);
			}
		}

		FVector2f Min(UE_BIG_NUMBER), Max(-UE_BIG_NUMBER);
		for (const FVector2f& P : Island.Points)
		{
			Min = FVector2f::Min(Min, P);
			Max = FVector2f::Max(Max, P);
		}
		for (FVector2f& P : Island.Points)
		{
			P -= Min;
		}
		Island.Size = Island.Points.IsEmpty() ? FVector2f::ZeroVector : Max - Min;
	}

	/**
	 * Shelf packing: rectangles (tallest first) go left to right into the first shelf with room, or start a new
	 * shelf below. Returns each rectangle's corner and the used extent.
	 */
	FVector2f ShelfPack(const TArray<FVector2f>& Sizes, const TArray<int32>& Order, float Width, TArray<FVector2f>& OutPositions)
	{
		struct FShelf { float Y; float X; };
		TArray<FShelf> Shelves;
		OutPositions.SetNumUninitialized(Sizes.Num());
		float Height = 0.0f;
		float MaxX = 0.0f;
		for (const int32 Index : Order)
		{
			const FVector2f& Size = Sizes[Index];
			FShelf* Shelf = Shelves.FindByPredicate([&](const FShelf& S) { return S.X + Size.X <= Width; });
			if (!Shelf)
			{
				// Sorted tallest first, so the first rectangle on a shelf sets its height
				Shelf = &Shelves.Add_GetRef({ Height, 0.0f });
				Height += Size.Y;
			}
			OutPositions[Index] = FVector2f(Shelf->X, Shelf->Y);
			Shelf->X += Size.X;
			MaxX = FMath::Max(MaxX, Shelf->X);
		}
		return FVector2f(MaxX, Height);
	}

	/** One packing attempt for a group at a given scale: island corners (local units, gap included) and the scale that fits. */
	struct FGroupPack
	{
		TArray<FVector2f> Positions;
		FVector2f Extent = FVector2f::ZeroVector;
		float GapLocal = 0.0f;
		float Fit = 0.0f;
	};

	/**
	 * Packs the group assuming the final scale is Scale (the gap is Gap in UV, so Gap / Scale before scaling),
	 * trying several shelf widths, and returns the best: the largest scale at which it fits the region.
	 */
	FGroupPack PackAtScale(const TArray<FIsland>& Islands, const TArray<int32>& Members, const FVector2f& Region, float Gap, float Scale)
	{
		FGroupPack Best;
		Best.GapLocal = Gap / FMath::Max(Scale, UE_SMALL_NUMBER);

		TArray<FVector2f> Sizes;
		Sizes.SetNumUninitialized(Members.Num());
		float Area = 0.0f;
		float Widest = 0.0f;
		for (int32 i = 0; i < Members.Num(); ++i)
		{
			Sizes[i] = Islands[Members[i]].Size + FVector2f(Best.GapLocal);
			Area += Sizes[i].X * Sizes[i].Y;
			Widest = FMath::Max(Widest, Sizes[i].X);
		}
		TArray<int32> Order;
		Order.SetNumUninitialized(Members.Num());
		for (int32 i = 0; i < Members.Num(); ++i)
		{
			Order[i] = i;
		}
		Order.Sort([&Sizes](int32 A, int32 B) { return Sizes[A].Y > Sizes[B].Y; });

		const float Aspect = Region.X / Region.Y;
		TArray<FVector2f> Positions;
		for (float Factor = 0.7f; Factor <= 1.61f; Factor += 0.05f)
		{
			const float Width = FMath::Max(FMath::Sqrt(Area * Aspect) * Factor, Widest);
			const FVector2f Extent = ShelfPack(Sizes, Order, Width, Positions);
			const float Fit = FMath::Min(Region.X / FMath::Max(Extent.X, UE_SMALL_NUMBER), Region.Y / FMath::Max(Extent.Y, UE_SMALL_NUMBER));
			if (Fit > Best.Fit)
			{
				Best.Fit = Fit;
				Best.Extent = Extent;
				Best.Positions = Positions;
			}
		}
		return Best;
	}

	/**
	 * The largest scale at which the group fits its region with exactly Gap between islands.
	 * MaxScale > 0 caps it (matched density across groups).
	 */
	float FindGroupScale(const TArray<FIsland>& Islands, const TArray<int32>& Members, const FVector2f& Region, float Gap, float MaxScale, FGroupPack& OutPack)
	{
		float Area = 0.0f;
		for (const int32 Member : Members)
		{
			Area += Islands[Member].Size.X * Islands[Member].Size.Y;
		}
		float Scale = MaxScale > 0.0f ? MaxScale : FMath::Sqrt(0.6f * Region.X * Region.Y / FMath::Max(Area, UE_SMALL_NUMBER));

		// 1. The gap depends on the scale and the scale on the gap: iterate toward the fixed point
		if (MaxScale <= 0.0f)
		{
			for (int32 Iteration = 0; Iteration < 6; ++Iteration)
			{
				Scale = PackAtScale(Islands, Members, Region, Gap, Scale).Fit;
			}
		}

		// 2. Shrink until the layout packed at this scale really fits at this scale (so the gap is at least Gap)
		for (int32 Iteration = 0; Iteration < 30; ++Iteration)
		{
			OutPack = PackAtScale(Islands, Members, Region, Gap, Scale);
			if (OutPack.Fit >= Scale)
			{
				break;
			}
			Scale = OutPack.Fit * 0.995f;
		}
		return Scale;
	}

	// SHAPE PACKING
	// Each island is drawn onto a grid (every cell its triangles touch, grown by half the padding), and placed
	// largest first where its top ends lowest, trying all four quarter turns. Islands fill each other's gaps and holes.
	// The largest scale that still fits is searched for, several scales at a time in parallel.

	/** Cells as bits, row by row, 64 per word: an island's footprint, or the region being filled. */
	struct FBitMask
	{
		int32 Width = 0;
		int32 Height = 0;
		int32 Words = 0;
		TArray<uint64> Bits;
		/** First set cell of each row (INDEX_NONE if the row is empty). */
		TArray<int32> FirstSet;

		void Init(int32 InWidth, int32 InHeight)
		{
			Width = FMath::Max(InWidth, 1);
			Height = FMath::Max(InHeight, 1);
			Words = (Width + 63) / 64;
			Bits.Init(0, Words * Height);
		}
		uint64* Row(int32 Y) { return Bits.GetData() + Y * Words; }
		const uint64* Row(int32 Y) const { return Bits.GetData() + Y * Words; }
		bool Get(int32 X, int32 Y) const { return (Row(Y)[X >> 6] >> (X & 63)) & 1; }
		void Set(int32 X, int32 Y) { Row(Y)[X >> 6] |= uint64(1) << (X & 63); }
		void FindFirstSet()
		{
			FirstSet.Init(INDEX_NONE, Height);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 W = 0; W < Words; ++W)
				{
					if (const uint64 Word = Row(Y)[W])
					{
						FirstSet[Y] = W * 64 + static_cast<int32>(FMath::CountTrailingZeros64(Word));
						break;
					}
				}
			}
		}
	};

	/** Does Mask, with its corner at (X, Y), hit a set cell of Grid? The mask must lie inside the grid. */
	bool Collides(const FBitMask& Grid, const FBitMask& Mask, int32 X, int32 Y)
	{
		const int32 Shift = X & 63;
		const int32 First = X >> 6;
		const bool bHasCarryWord = First + Mask.Words < Grid.Words;
		for (int32 R = 0; R < Mask.Height; ++R)
		{
			const uint64* G = Grid.Row(Y + R) + First;
			const uint64* M = Mask.Row(R);
			uint64 Carry = 0;
			for (int32 W = 0; W < Mask.Words; ++W)
			{
				if (((M[W] << Shift) | Carry) & G[W])
				{
					return true;
				}
				Carry = Shift ? M[W] >> (64 - Shift) : 0;
			}
			if (Carry && bHasCarryWord && (Carry & G[Mask.Words]))
			{
				return true;
			}
		}
		return false;
	}

	/** Sets Mask's cells in Grid, with its corner at (X, Y). */
	void Stamp(FBitMask& Grid, const FBitMask& Mask, int32 X, int32 Y)
	{
		const int32 Shift = X & 63;
		const int32 First = X >> 6;
		for (int32 R = 0; R < Mask.Height; ++R)
		{
			uint64* G = Grid.Row(Y + R) + First;
			const uint64* M = Mask.Row(R);
			uint64 Carry = 0;
			for (int32 W = 0; W < Mask.Words; ++W)
			{
				G[W] |= (M[W] << Shift) | Carry;
				Carry = Shift ? M[W] >> (64 - Shift) : 0;
			}
			if (Carry)
			{
				G[Mask.Words] |= Carry; // only set when those cells are inside the grid
			}
		}
	}

	/** Moves FirstFree[Y] (every cell left of it is taken) past the cells now taken. */
	void AdvanceFirstFree(const FBitMask& Grid, TArray<int32>& FirstFree, int32 Y)
	{
		int32 X = FirstFree[Y];
		while (X < Grid.Width)
		{
			const uint64 Free = ~Grid.Row(Y)[X >> 6] >> (X & 63);
			if (Free)
			{
				X += static_cast<int32>(FMath::CountTrailingZeros64(Free));
				break;
			}
			X = (X | 63) + 1;
		}
		FirstFree[Y] = FMath::Min(X, Grid.Width);
	}

	/** Does the triangle touch the unit cell at (X, Y)? Separating axes: the cell's (done by the caller's bounds) and the edges' normals. */
	bool TriangleTouchesCell(const FVector2f T[3], float X, float Y)
	{
		const FVector2f Box[4] = { FVector2f(X, Y), FVector2f(X + 1.0f, Y), FVector2f(X, Y + 1.0f), FVector2f(X + 1.0f, Y + 1.0f) };
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const FVector2f& A = T[Edge];
			const FVector2f& B = T[(Edge + 1) % 3];
			const FVector2f Normal(A.Y - B.Y, B.X - A.X);
			const float Opposite = FVector2f::DotProduct(Normal, T[(Edge + 2) % 3] - A);
			float BoxMin = UE_BIG_NUMBER;
			float BoxMax = -UE_BIG_NUMBER;
			for (const FVector2f& Corner : Box)
			{
				const float D = FVector2f::DotProduct(Normal, Corner - A);
				BoxMin = FMath::Min(BoxMin, D);
				BoxMax = FMath::Max(BoxMax, D);
			}
			const float Epsilon = 1e-5f * (FMath::Abs(Normal.X) + FMath::Abs(Normal.Y));
			if (BoxMax < FMath::Min(0.0f, Opposite) - Epsilon || BoxMin > FMath::Max(0.0f, Opposite) + Epsilon)
			{
				return false;
			}
		}
		return true;
	}

	/** Sets every cell within Half cells (along the line) of a set cell. */
	void Grow1D(const uint8* In, uint8* Out, int32 Num, int32 Stride, int32 Half)
	{
		int32 Last = -Half - 1;
		for (int32 i = 0; i < Num; ++i)
		{
			if (In[i * Stride])
			{
				Last = i;
			}
			Out[i * Stride] = i - Last <= Half;
		}
		Last = Num + Half;
		for (int32 i = Num - 1; i >= 0; --i)
		{
			if (In[i * Stride])
			{
				Last = i;
			}
			Out[i * Stride] |= Last - i <= Half;
		}
	}

	/**
	 * The island's footprint at CellScale (cells per layout unit): every cell its triangles touch, grown by Half
	 * cells all round. The island's corner (0, 0) sits at (Half, Half) in the mask.
	 */
	void RasterizeIsland(const FIsland& Island, float CellScale, int32 Half, FBitMask& Out)
	{
		const FVector2f Extent = Island.Size * CellScale;
		const int32 Width = FMath::Max(FMath::CeilToInt32(Extent.X), 1) + 2 * Half;
		const int32 Height = FMath::Max(FMath::CeilToInt32(Extent.Y), 1) + 2 * Half;
		TArray<uint8> Solid;
		Solid.SetNumZeroed(Width * Height);
		for (int32 Corner = 0; Corner + 2 < Island.Corners.Num(); Corner += 3)
		{
			FVector2f T[3];
			FVector2f Min(UE_BIG_NUMBER), Max(-UE_BIG_NUMBER);
			for (int32 i = 0; i < 3; ++i)
			{
				T[i] = Island.Points[Island.Corners[Corner + i]] * CellScale + FVector2f(static_cast<float>(Half));
				Min = FVector2f::Min(Min, T[i]);
				Max = FVector2f::Max(Max, T[i]);
			}
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32(Min.X), 0, Width - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(Min.Y), 0, Height - 1);
			const int32 X1 = FMath::Clamp(FMath::CeilToInt32(Max.X) - 1, X0, Width - 1);
			const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(Max.Y) - 1, Y0, Height - 1);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					uint8& Cell = Solid[Y * Width + X];
					if (!Cell && TriangleTouchesCell(T, static_cast<float>(X), static_cast<float>(Y)))
					{
						Cell = 1;
					}
				}
			}
		}

		if (Half > 0)
		{
			TArray<uint8> Grown;
			Grown.SetNumZeroed(Width * Height);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				Grow1D(&Solid[Y * Width], &Grown[Y * Width], Width, 1, Half);
			}
			for (int32 X = 0; X < Width; ++X)
			{
				Grow1D(&Grown[X], &Solid[X], Height, Width, Half);
			}
		}

		Out.Init(Width, Height);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (Solid[Y * Width + X])
				{
					Out.Set(X, Y);
				}
			}
		}
		Out.FindFirstSet();
	}

	/** The mask turned Turns quarter turns counterclockwise, in the same way as TurnPoint. */
	void TurnMask(const FBitMask& In, int32 Turns, FBitMask& Out)
	{
		const bool bSwap = (Turns & 1) != 0;
		Out.Init(bSwap ? In.Height : In.Width, bSwap ? In.Width : In.Height);
		for (int32 Y = 0; Y < In.Height; ++Y)
		{
			for (int32 X = 0; X < In.Width; ++X)
			{
				if (!In.Get(X, Y))
				{
					continue;
				}
				switch (Turns)
				{
				case 1: Out.Set(In.Height - 1 - Y, X); break;
				case 2: Out.Set(In.Width - 1 - X, In.Height - 1 - Y); break;
				case 3: Out.Set(Y, In.Width - 1 - X); break;
				default: Out.Set(X, Y); break;
				}
			}
		}
		Out.FindFirstSet();
	}

	/** A point of a Width x Height frame turned with it (the turned frame's corner stays at the origin). No mirroring. */
	FVector2f TurnPoint(const FVector2f& P, int32 Turns, float Width, float Height)
	{
		switch (Turns)
		{
		case 1: return FVector2f(Height - P.Y, P.X);
		case 2: return FVector2f(Width - P.X, Height - P.Y);
		case 3: return FVector2f(P.Y, Width - P.X);
		default: return P;
		}
	}

	/** A shape packing of one group: the cell corner, quarter turns and unturned mask size of each member. */
	struct FShapePack
	{
		TArray<FIntPoint> Cells;
		TArray<uint8> Turns;
		TArray<FIntPoint> Frames;
		FIntPoint GridSize = FIntPoint::ZeroValue;
		FIntPoint Used = FIntPoint::ZeroValue;
		float Scale = 0.0f;
		bool bFits = false;
	};

	/** Packs the members at Scale (UV per layout unit), largest first, each turned to where its top ends lowest, then leftmost. */
	void PackShapesAtScale(const TArray<FIsland>& Islands, const TArray<int32>& Members, FIntPoint GridSize, float CellsPerUV, int32 Half, bool bTurn, float Scale, FShapePack& Out)
	{
		Out.Scale = Scale;
		Out.GridSize = GridSize;
		Out.bFits = false;
		const float CellScale = Scale * CellsPerUV;
		const int32 NumTurns = bTurn ? 4 : 1;
		const int32 Num = Members.Num();

		TArray<FBitMask> Masks;
		Masks.SetNum(Num * NumTurns);
		for (int32 i = 0; i < Num; ++i)
		{
			FBitMask& Base = Masks[i * NumTurns];
			RasterizeIsland(Islands[Members[i]], CellScale, Half, Base);
			const bool bFitsFlat = Base.Width <= GridSize.X && Base.Height <= GridSize.Y;
			const bool bFitsTurned = bTurn && Base.Height <= GridSize.X && Base.Width <= GridSize.Y;
			if (!bFitsFlat && !bFitsTurned)
			{
				return; // too big at this scale
			}
			for (int32 Turn = 1; Turn < NumTurns; ++Turn)
			{
				TurnMask(Base, Turn, Masks[i * NumTurns + Turn]);
			}
		}

		TArray<int32> Order;
		for (int32 i = 0; i < Num; ++i)
		{
			Order.Add(i);
		}
		Order.Sort([&](int32 A, int32 B)
		{
			const FBitMask& MA = Masks[A * NumTurns];
			const FBitMask& MB = Masks[B * NumTurns];
			return MA.Width * MA.Height > MB.Width * MB.Height;
		});

		FBitMask Grid;
		Grid.Init(GridSize.X, GridSize.Y);
		TArray<int32> FirstFree;
		FirstFree.Init(0, Grid.Height);
		Out.Cells.SetNum(Num);
		Out.Turns.SetNum(Num);
		Out.Frames.SetNum(Num);
		Out.Used = FIntPoint::ZeroValue;

		for (const int32 i : Order)
		{
			int32 BestTop = MAX_int32;
			int32 BestX = 0;
			int32 BestY = 0;
			int32 BestTurn = INDEX_NONE;
			for (int32 Turn = 0; Turn < NumTurns; ++Turn)
			{
				const FBitMask& Mask = Masks[i * NumTurns + Turn];
				if (Mask.Width > Grid.Width || Mask.Height > Grid.Height)
				{
					continue;
				}
				// Rows from the bottom; only a lower top than the best turn so far helps
				for (int32 Y = 0; Y + Mask.Height <= Grid.Height && Y + Mask.Height < BestTop; ++Y)
				{
					// Every cell left of FirstFree is taken, so each row's first cell must land at or after it
					int32 X = 0;
					for (int32 R = 0; R < Mask.Height; ++R)
					{
						if (Mask.FirstSet[R] != INDEX_NONE)
						{
							X = FMath::Max(X, FirstFree[Y + R] - Mask.FirstSet[R]);
						}
					}
					for (; X + Mask.Width <= Grid.Width; ++X)
					{
						if (!Collides(Grid, Mask, X, Y))
						{
							BestTop = Y + Mask.Height;
							BestX = X;
							BestY = Y;
							BestTurn = Turn;
							break;
						}
					}
					if (BestTurn == Turn)
					{
						break;
					}
				}
			}
			if (BestTurn == INDEX_NONE)
			{
				return; // no room left
			}

			const FBitMask& Mask = Masks[i * NumTurns + BestTurn];
			Stamp(Grid, Mask, BestX, BestY);
			for (int32 R = 0; R < Mask.Height; ++R)
			{
				AdvanceFirstFree(Grid, FirstFree, BestY + R);
			}
			Out.Cells[i] = FIntPoint(BestX, BestY);
			Out.Turns[i] = static_cast<uint8>(BestTurn);
			Out.Frames[i] = FIntPoint(Masks[i * NumTurns].Width, Masks[i * NumTurns].Height);
			Out.Used = FIntPoint(FMath::Max(Out.Used.X, BestX + Mask.Width), FMath::Max(Out.Used.Y, BestY + Mask.Height));
		}
		Out.bFits = true;
	}

	/**
	 * The largest scale at which the group's shapes pack into its region: 8 scales at a time in parallel, then twice
	 * more between the largest that fit and the next. MaxScale > 0 caps it (matched density). 0 if nothing fits.
	 */
	float FindShapeScale(const TArray<FIsland>& Islands, const TArray<int32>& Members, const FVector2f& Region, float CellsPerUV, int32 Half, bool bTurn, float MaxScale, FShapePack& OutPack)
	{
		const FIntPoint GridSize(FMath::Max(FMath::FloorToInt32(Region.X * CellsPerUV + 0.01f), 1), FMath::Max(FMath::FloorToInt32(Region.Y * CellsPerUV + 0.01f), 1));
		double Area = 0.0;
		for (const int32 Member : Members)
		{
			Area += Islands[Member].Area;
		}

		// Islands can't cover more than the whole region, so this is the most the scale can be
		float Hi = FMath::Sqrt(static_cast<float>(Region.X * Region.Y / FMath::Max(Area, 1e-12)));
		if (MaxScale > 0.0f)
		{
			Hi = FMath::Min(Hi, MaxScale);
		}
		float Lo = Hi * 0.25f;

		constexpr int32 NumTries = 8;
		FShapePack Best;
		// Index of the largest scale in [From, To] that fits, or INDEX_NONE; keeps it in Best when it beats it
		auto TryRange = [&](float From, float To) -> int32
		{
			TArray<FShapePack> Packs;
			Packs.SetNum(NumTries);
			ParallelFor(NumTries, [&](int32 i)
			{
				const float Scale = From * FMath::Pow(To / From, static_cast<float>(i) / (NumTries - 1));
				PackShapesAtScale(Islands, Members, GridSize, CellsPerUV, Half, bTurn, Scale, Packs[i]);
			});
			for (int32 i = NumTries - 1; i >= 0; --i)
			{
				if (Packs[i].bFits)
				{
					if (Packs[i].Scale > Best.Scale)
					{
						Best = MoveTemp(Packs[i]);
					}
					return i;
				}
			}
			return INDEX_NONE;
		};

		int32 Found = TryRange(Lo, Hi);
		// Nothing fit (lots of padding around tiny islands): look lower
		for (int32 Attempt = 0; Found == INDEX_NONE && Attempt < 6; ++Attempt)
		{
			Hi = Lo;
			Lo *= 0.25f;
			Found = TryRange(Lo, Hi);
		}
		if (Found == INDEX_NONE)
		{
			return 0.0f;
		}

		// Refine between the largest fit and the next scale tried
		float Step = FMath::Pow(Hi / Lo, 1.0f / (NumTries - 1));
		for (int32 Round = 0; Round < 2 && Found < NumTries - 1; ++Round)
		{
			const float From = Best.Scale;
			Found = TryRange(From, From * Step);
			Step = FMath::Pow(Step, 1.0f / (NumTries - 1));
		}

		OutPack = MoveTemp(Best);
		return OutPack.Scale;
	}

	/**
	 * Most regions side by side in one row; more than this start another row.
	 * A material node that rebuilds this layout in the shader (e.g. a UV Region node) must be kept in step with it.
	 */
	constexpr int32 MaxColumns = 3;

	/**
	 * One region per group, filling the whole 0-1 square with no empty cells. Groups go into columns along U first:
	 * 1 group the whole square, 2 two columns, 3 three columns. More groups use ceil(N / 3) rows, shared out as
	 * evenly as possible (earlier rows take the extra), and each row is split evenly among its groups:
	 * 4 = 2 + 2, 5 = 3 + 2, 6 = 3 + 3, 7 = 3 + 2 + 2.
	 */
	TArray<FBox2f> MakeRegions(int32 NumGroups)
	{
		TArray<FBox2f> Regions;
		const int32 NumRows = FMath::DivideAndRoundUp(FMath::Max(NumGroups, 1), MaxColumns);
		const float RowHeight = 1.0f / NumRows;
		int32 Remaining = FMath::Max(NumGroups, 1);
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const int32 InRow = FMath::DivideAndRoundUp(Remaining, NumRows - Row);
			Remaining -= InRow;
			const float Width = 1.0f / InRow;
			for (int32 Column = 0; Column < InRow; ++Column)
			{
				const FVector2f Min(Column * Width, Row * RowHeight);
				Regions.Add(FBox2f(Min, Min + FVector2f(Width, RowHeight)));
			}
		}
		return Regions;
	}

	/** Tiles per UDIM row (1001-1010), as in the UDIM standard. */
	constexpr int32 UDIMColumns = 10;

	/**
	 * One whole UDIM tile per group: group i is tile 1001 + i, ten to a row. Further rows go one unit down in V
	 * (negative V), where Unreal's FBX import puts tile 1011 and up and its export brings them back.
	 * The Vrr UV Region material node (UDIM on) rebuilds this: keep both in step.
	 */
	TArray<FBox2f> MakeUDIMTiles(int32 NumGroups)
	{
		TArray<FBox2f> Tiles;
		for (int32 Group = 0; Group < FMath::Max(NumGroups, 1); ++Group)
		{
			const FVector2f Min(static_cast<float>(Group % UDIMColumns), -static_cast<float>(Group / UDIMColumns));
			Tiles.Add(FBox2f(Min, Min + FVector2f(1.0f)));
		}
		return Tiles;
	}

	/** The mesh's material slot names, in slot order. */
	TArray<FName> GetSlotOrder(const UObject* Mesh)
	{
		TArray<FName> Names;
		if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
			{
				Names.Add(Material.ImportedMaterialSlotName);
			}
		}
		else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			for (const FSkeletalMaterial& Material : SkeletalMesh->GetMaterials())
			{
				Names.Add(Material.ImportedMaterialSlotName);
			}
		}
		return Names;
	}

	/** Steps 1-5 of the header comment for one LOD. SlotOrder numbers the regions (see GetSlotOrder). */
	bool BuildLayout(const FMeshDescription& MeshDescription, const TArray<FName>& SlotOrder, const FUVLayoutSettings& Settings, FLayout& Out, FText& OutError)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		TVertexInstanceAttributesConstRef<FVector2f> SourceUVs = Attributes.GetVertexInstanceUVs();
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		if (Settings.SourceChannel < 0 || Settings.SourceChannel >= SourceUVs.GetNumChannels())
		{
			OutError = FText::Format(LOCTEXT("NoSource", "The mesh has no UV channel {0}."), Settings.SourceChannel);
			return false;
		}

		// Triangles with their corners and group (material slot)
		TArray<FVertexInstanceID> Corners;
		TArray<int32> TriangleGroup;
		TMap<FName, int32> GroupOfSlot;
		TArray<FName> GroupNames;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			Corners.Append(MeshDescription.GetTriangleVertexInstances(TriangleID));
			int32 Group = 0;
			if (Settings.bGroupByMaterial)
			{
				const FPolygonGroupID PolygonGroup = MeshDescription.GetTrianglePolygonGroup(TriangleID);
				FName Slot = SlotNames.IsValid() ? SlotNames[PolygonGroup] : NAME_None;
				if (Slot.IsNone())
				{
					Slot = FName(*FString::Printf(TEXT("Section %d"), PolygonGroup.GetValue()));
				}
				const int32* Found = GroupOfSlot.Find(Slot);
				Group = Found ? *Found : GroupOfSlot.Add(Slot, GroupNames.Add(Slot));
			}
			else if (GroupNames.IsEmpty())
			{
				GroupNames.Add(TEXT("All"));
			}
			TriangleGroup.Add(Group);
		}

		// Number the groups in material slot order (not the order triangles happen to come in), so Region N in the
		// Vrr UV Region material node is the Nth used slot. Names the mesh doesn't list go last.
		if (GroupNames.Num() > 1)
		{
			TArray<int32> Order;
			for (int32 Group = 0; Group < GroupNames.Num(); ++Group)
			{
				Order.Add(Group);
			}
			auto SlotIndex = [&](int32 Group) { const int32 Index = SlotOrder.IndexOfByKey(GroupNames[Group]); return Index == INDEX_NONE ? MAX_int32 : Index; };
			Order.StableSort([&](int32 A, int32 B) { return SlotIndex(A) < SlotIndex(B); });

			TArray<int32> NewIndex;
			NewIndex.SetNumUninitialized(Order.Num());
			TArray<FName> SortedNames;
			for (int32 i = 0; i < Order.Num(); ++i)
			{
				NewIndex[Order[i]] = i;
				SortedNames.Add(GroupNames[Order[i]]);
			}
			GroupNames = MoveTemp(SortedNames);
			for (int32& Group : TriangleGroup)
			{
				Group = NewIndex[Group];
			}
		}
		const int32 NumTriangles = TriangleGroup.Num();
		if (NumTriangles == 0)
		{
			OutError = LOCTEXT("NoTriangles", "The mesh has no triangles.");
			return false;
		}

		// 1. Islands: join triangles sharing a vertex instance, or a corner with the same position, UV and group
		FUnionFind Sets(NumTriangles);
		TMap<int32, int32> TriangleOfInstance;
		TMap<FCornerKey, int32> KeyIds;
		TArray<int32> CornerKeyId;
		CornerKeyId.SetNumUninitialized(NumTriangles * 3);
		TArray<int32> TriangleOfKey;
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			for (int32 i = 0; i < 3; ++i)
			{
				const FVertexInstanceID Instance = Corners[Triangle * 3 + i];
				if (const int32* Other = TriangleOfInstance.Find(Instance.GetValue()))
				{
					Sets.Union(Triangle, *Other);
				}
				else
				{
					TriangleOfInstance.Add(Instance.GetValue(), Triangle);
				}

				const FVector2f UV = SourceUVs.Get(Instance, Settings.SourceChannel);
				const FCornerKey Key{
					MeshDescription.GetVertexInstanceVertex(Instance).GetValue(),
					FMath::RoundToInt32(FMath::Clamp(UV.X, -16000.0f, 16000.0f) * 65536.0f),
					FMath::RoundToInt32(FMath::Clamp(UV.Y, -16000.0f, 16000.0f) * 65536.0f),
					TriangleGroup[Triangle] };
				const int32* Id = KeyIds.Find(Key);
				if (Id)
				{
					Sets.Union(Triangle, TriangleOfKey[*Id]);
					CornerKeyId[Triangle * 3 + i] = *Id;
				}
				else
				{
					CornerKeyId[Triangle * 3 + i] = KeyIds.Add(Key, TriangleOfKey.Add(Triangle));
				}
			}
		}

		TArray<FIsland> Islands;
		TArray<int32> IslandOfTriangle;
		IslandOfTriangle.SetNumUninitialized(NumTriangles);
		{
			TMap<int32, int32> IslandOfRoot;
			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
			{
				const int32 Root = Sets.Find(Triangle);
				const int32* Found = IslandOfRoot.Find(Root);
				const int32 Island = Found ? *Found : IslandOfRoot.Add(Root, Islands.AddDefaulted());
				Islands[Island].Triangles.Add(Triangle);
				IslandOfTriangle[Triangle] = Island;
			}
		}

		// 2. Per island: unique corners, density scale (or a flat projection), orientation
		int32 NumProjected = 0;
		for (FIsland& Island : Islands)
		{
			TMap<int32, int32> LocalOfInstance;
			double Area3D = 0.0;
			double AreaUV = 0.0;
			FVector3f NormalSum = FVector3f::ZeroVector;
			TMap<int32, double> GroupArea;
			for (const int32 Triangle : Island.Triangles)
			{
				FVector3f P[3];
				FVector2f T[3];
				for (int32 i = 0; i < 3; ++i)
				{
					const FVertexInstanceID Instance = Corners[Triangle * 3 + i];
					P[i] = Positions[MeshDescription.GetVertexInstanceVertex(Instance)];
					T[i] = SourceUVs.Get(Instance, Settings.SourceChannel);
					if (!LocalOfInstance.Contains(Instance.GetValue()))
					{
						LocalOfInstance.Add(Instance.GetValue(), Island.Instances.Add(Instance.GetValue()));
						Island.Points.Add(T[i]);
					}
					Island.Corners.Add(LocalOfInstance.FindChecked(Instance.GetValue()));
				}
				const FVector3f Cross = FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]);
				const double TriangleArea = 0.5 * Cross.Size();
				Area3D += TriangleArea;
				AreaUV += 0.5 * FMath::Abs(Cross2D(T[1] - T[0], T[2] - T[0]));
				NormalSum += Cross;
				GroupArea.FindOrAdd(TriangleGroup[Triangle]) += TriangleArea + UE_SMALL_NUMBER;
			}

			Island.Area = static_cast<float>(Area3D);

			// The island's region: the group holding most of its area (islands only span groups through shared vertex instances)
			double BestArea = -1.0;
			for (const TPair<int32, double>& Pair : GroupArea)
			{
				if (Pair.Value > BestArea)
				{
					BestArea = Pair.Value;
					Island.Group = Pair.Key;
				}
			}

			if (AreaUV < 1e-12 && Area3D > 0.0)
			{
				// No UV area to scale: lay the island flat along its average normal (already in cm)
				FVector3f Normal = NormalSum.GetSafeNormal();
				if (Normal.IsNearlyZero())
				{
					Normal = FVector3f::UnitZ();
				}
				FVector3f AxisU, AxisV;
				Normal.FindBestAxisVectors(AxisU, AxisV);
				for (int32 i = 0; i < Island.Instances.Num(); ++i)
				{
					const FVector3f Position = Positions[MeshDescription.GetVertexInstanceVertex(FVertexInstanceID(Island.Instances[i]))];
					Island.Points[i] = FVector2f(FVector3f::DotProduct(Position, AxisU), FVector3f::DotProduct(Position, AxisV));
				}
				Island.bProjected = true;
				++NumProjected;
			}
			else
			{
				// Same texel density everywhere: UV units -> cm
				const float Scale = AreaUV > 0.0 ? static_cast<float>(FMath::Sqrt(Area3D / AreaUV)) : 0.0f;
				for (FVector2f& Point : Island.Points)
				{
					Point *= Scale;
				}
			}
			OrientIsland(Island, Settings.bRotateIslands);
		}

		// 3. Regions: columns first, every row split evenly, so the whole square is used; or one UDIM tile per group
		const int32 NumGroups = FMath::Max(GroupNames.Num(), 1);
		const bool bUDIM = Settings.bGroupAsUDIMs && Settings.bGroupByMaterial;
		const TArray<FBox2f> Regions = bUDIM ? MakeUDIMTiles(NumGroups) : MakeRegions(NumGroups);
		const float Gap = static_cast<float>(FMath::Max(Settings.Padding, 0)) / FMath::Max(Settings.PaddingResolution, 1);

		TArray<TArray<int32>> Members;
		Members.SetNum(NumGroups);
		for (int32 Island = 0; Island < Islands.Num(); ++Island)
		{
			Members[Islands[Island].Group].Add(Island);
		}

		// Islands lie flat (wider than tall). In a region taller than wide (a column), stand them up instead,
		// so long islands don't have to fit across the narrow side
		if (Settings.bRotateIslands)
		{
			for (FIsland& Island : Islands)
			{
				const FVector2f RegionSize = Regions[Island.Group].GetSize();
				if (RegionSize.Y > RegionSize.X * 1.01f)
				{
					// 90 degrees: (x, y) -> (y, width - x), so the corner stays at the origin
					for (FVector2f& Point : Island.Points)
					{
						Point = FVector2f(Point.Y, Island.Size.X - Point.X);
					}
					Island.Size = FVector2f(Island.Size.Y, Island.Size.X);
				}
			}
		}

		// 4. Pack each group (shapes or boxes); with matched density, all at the smallest group's scale.
		// Shapes: a grid of CellsPerUV cells per UV unit (at most the padding resolution, so lightmaps pack per
		// texel), each island grown by Half cells, so neighbours are at least the padding apart
		const float CellsPerUV = static_cast<float>(FMath::Clamp(FMath::Min(Settings.PackDetail, Settings.PaddingResolution), 16, 4096));
		const int32 PaddingCells = FMath::CeilToInt32(FMath::Max(Settings.Padding, 0) * CellsPerUV / FMath::Max(Settings.PaddingResolution, 1) - 1e-4f);
		const int32 Half = (PaddingCells + 1) / 2;
		TArray<bool> UseShapes;
		UseShapes.Init(Settings.PackMethod == EUVPackMethod::Shapes, NumGroups);
		TArray<float> Scales;
		TArray<FGroupPack> Packs;
		TArray<FShapePack> ShapePacks;
		Scales.SetNumZeroed(NumGroups);
		Packs.SetNum(NumGroups);
		ShapePacks.SetNum(NumGroups);
		auto PackGroup = [&](int32 Group, float MaxScale)
		{
			const FVector2f RegionSize = Regions[Group].GetSize();
			if (UseShapes[Group])
			{
				Scales[Group] = FindShapeScale(Islands, Members[Group], RegionSize, CellsPerUV, Half, Settings.bRotateIslands, MaxScale, ShapePacks[Group]);
				if (Scales[Group] > 0.0f)
				{
					return;
				}
				UseShapes[Group] = false; // nothing fit on the grid: boxes instead
			}
			Scales[Group] = FindGroupScale(Islands, Members[Group], RegionSize, Gap, MaxScale, Packs[Group]);
		};
		for (int32 Group = 0; Group < NumGroups; ++Group)
		{
			if (!Members[Group].IsEmpty())
			{
				PackGroup(Group, 0.0f);
			}
		}
		if (Settings.bMatchDensityAcrossMaterials && NumGroups > 1)
		{
			float Smallest = UE_BIG_NUMBER;
			for (int32 Group = 0; Group < NumGroups; ++Group)
			{
				if (!Members[Group].IsEmpty())
				{
					Smallest = FMath::Min(Smallest, Scales[Group]);
				}
			}
			for (int32 Group = 0; Group < NumGroups; ++Group)
			{
				if (!Members[Group].IsEmpty())
				{
					PackGroup(Group, Smallest);
				}
			}
		}

		// 5. Place each group in its region, centred; optionally stretch it to the region's edges
		Out.Preview = FUVLayoutPreview();
		Out.Preview.bUDIM = bUDIM;
		Out.UVs.Init(FVector2f::ZeroVector, MeshDescription.VertexInstances().GetArraySize());
		for (int32 Group = 0; Group < NumGroups; ++Group)
		{
			const FVector2f RegionMin = Regions[Group].Min;
			Out.Preview.Regions.Add(Regions[Group]);
			Out.Preview.RegionNames.Add(GroupNames.IsValidIndex(Group) ? GroupNames[Group] : NAME_None);
			if (Members[Group].IsEmpty())
			{
				continue;
			}

			const float Scale = Scales[Group];
			float EdgeMargin = Gap * 0.5f;
			if (UseShapes[Group])
			{
				// Grid cell corner + the island turned in its mask frame; whole cells of centering keep it on the texel grid
				const FShapePack& Pack = ShapePacks[Group];
				const FVector2f Centering(static_cast<float>((Pack.GridSize.X - Pack.Used.X) / 2), static_cast<float>((Pack.GridSize.Y - Pack.Used.Y) / 2));
				const float CellScale = Scale * CellsPerUV;
				for (int32 i = 0; i < Members[Group].Num(); ++i)
				{
					const FIsland& Island = Islands[Members[Group][i]];
					const FVector2f Corner = FVector2f(static_cast<float>(Pack.Cells[i].X), static_cast<float>(Pack.Cells[i].Y)) + Centering;
					for (int32 j = 0; j < Island.Instances.Num(); ++j)
					{
						const FVector2f InFrame = Island.Points[j] * CellScale + FVector2f(static_cast<float>(Half));
						const FVector2f Turned = TurnPoint(InFrame, Pack.Turns[i], static_cast<float>(Pack.Frames[i].X), static_cast<float>(Pack.Frames[i].Y));
						Out.UVs[Island.Instances[j]] = RegionMin + (Corner + Turned) / CellsPerUV;
					}
				}
				EdgeMargin = Half / CellsPerUV;
			}
			else
			{
				// Region corner + centering + packed corner (half a gap in), all scaled
				const FGroupPack& Pack = Packs[Group];
				const FVector2f Centering = (Regions[Group].GetSize() - Pack.Extent * Scale) * 0.5f;
				for (int32 i = 0; i < Members[Group].Num(); ++i)
				{
					const FIsland& Island = Islands[Members[Group][i]];
					const FVector2f Origin = RegionMin + Centering + (Pack.Positions[i] + FVector2f(Pack.GapLocal * 0.5f)) * Scale;
					for (int32 j = 0; j < Island.Instances.Num(); ++j)
					{
						Out.UVs[Island.Instances[j]] = Origin + Island.Points[j] * Scale;
					}
				}
			}

			// Stretch: scale each axis so the islands reach the region's edges (gaps only grow, so padding holds)
			if (Settings.bStretchToFill)
			{
				FBox2f Used(ForceInit);
				for (const int32 Member : Members[Group])
				{
					for (const int32 Instance : Islands[Member].Instances)
					{
						Used += Out.UVs[Instance];
					}
				}
				const FBox2f Target(Regions[Group].Min + FVector2f(EdgeMargin), Regions[Group].Max - FVector2f(EdgeMargin));
				const FVector2f UsedSize = Used.GetSize();
				const FVector2f TargetSize = Target.GetSize();
				const FVector2f Factor(
					UsedSize.X > UE_KINDA_SMALL_NUMBER ? FMath::Max(TargetSize.X / UsedSize.X, 1.0f) : 1.0f,
					UsedSize.Y > UE_KINDA_SMALL_NUMBER ? FMath::Max(TargetSize.Y / UsedSize.Y, 1.0f) : 1.0f);
				const FVector2f UsedCenter = Used.GetCenter();
				const FVector2f TargetCenter = Target.GetCenter();
				for (const int32 Member : Members[Group])
				{
					for (const int32 Instance : Islands[Member].Instances)
					{
						Out.UVs[Instance] = TargetCenter + (Out.UVs[Instance] - UsedCenter) * Factor;
					}
				}
			}
		}

		// Preview: triangles by region, island outlines (edges used by one triangle of the island), coverage
		FUVLayoutPreview& Preview = Out.Preview;
		Preview.NumIslands = Islands.Num();
		Preview.NumProjected = NumProjected;
		Preview.Triangles.Reserve(NumTriangles * 3);
		Preview.TriangleGroups.Reserve(NumTriangles);
		TMap<uint64, int32> EdgeUses;
		double Covered = 0.0;
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			FVector2f T[3];
			for (int32 i = 0; i < 3; ++i)
			{
				T[i] = Out.UVs[Corners[Triangle * 3 + i].GetValue()];
				Preview.Triangles.Add(T[i]);
			}
			Preview.TriangleGroups.Add(Islands[IslandOfTriangle[Triangle]].Group);
			Covered += 0.5 * FMath::Abs(Cross2D(T[1] - T[0], T[2] - T[0]));

			for (int32 i = 0; i < 3; ++i)
			{
				uint32 A = static_cast<uint32>(CornerKeyId[Triangle * 3 + i]);
				uint32 B = static_cast<uint32>(CornerKeyId[Triangle * 3 + (i + 1) % 3]);
				if (A > B)
				{
					Swap(A, B);
				}
				EdgeUses.FindOrAdd((static_cast<uint64>(A) << 32) | B)++;
			}
		}
		double RegionArea = 0.0;
		for (const FBox2f& Region : Regions)
		{
			RegionArea += Region.GetArea();
		}
		Preview.Coverage = static_cast<float>(Covered / FMath::Max(RegionArea, 1e-12));
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			for (int32 i = 0; i < 3; ++i)
			{
				uint32 A = static_cast<uint32>(CornerKeyId[Triangle * 3 + i]);
				uint32 B = static_cast<uint32>(CornerKeyId[Triangle * 3 + (i + 1) % 3]);
				if (A > B)
				{
					Swap(A, B);
				}
				if (EdgeUses.FindChecked((static_cast<uint64>(A) << 32) | B) == 1)
				{
					Preview.Outlines.Add(Out.UVs[Corners[Triangle * 3 + i].GetValue()]);
					Preview.Outlines.Add(Out.UVs[Corners[Triangle * 3 + (i + 1) % 3].GetValue()]);
				}
			}
		}
		return true;
	}

	/** Undo support and the rebuild hook for the LOD's source data. */
	void ModifyMeshDescription(UObject* Mesh, int32 LODIndex)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			StaticMesh->ModifyMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			SkeletalMesh->ModifyMeshDescription(LODIndex);
		}
	}

	void CommitMeshDescription(UObject* Mesh, int32 LODIndex)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			StaticMesh->CommitMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			SkeletalMesh->CommitMeshDescription(LODIndex);
		}
	}

	/** Does the lightmap of this static mesh LOD use Channel (lightmap index, or Generate Lightmap UVs writing there)? */
	bool IsLightmapChannel(const UStaticMesh* StaticMesh, int32 LODIndex, int32 Channel)
	{
		if (!StaticMesh)
		{
			return false;
		}
		if (StaticMesh->GetLightMapCoordinateIndex() == Channel)
		{
			return true;
		}
		const FMeshBuildSettings& Build = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
		return Build.bGenerateLightmapUVs && Build.DstLightmapIndex == Channel;
	}

	/**
	 * Lays out every LOD before anything is changed, so a failure leaves the mesh as it was.
	 * LODs that can't be laid out (or that Fits rejects for their channel count) are removed from InOutLODs and
	 * listed in OutSkipped; LOD 0 failing fails the whole thing (false, with OutError).
	 */
	bool LayOutLODs(UObject* Mesh, const FUVLayoutSettings& Settings, TFunctionRef<bool(int32 LOD, int32 NumChannels)> Fits,
		TArray<int32>& InOutLODs, TArray<FLayout>& OutLayouts, TArray<int32>& OutSkipped, FScopedSlowTask& SlowTask, FText& OutError)
	{
		const TArray<FName> SlotOrder = GetSlotOrder(Mesh);
		for (int32 i = 0; i < InOutLODs.Num(); ++i)
		{
			const int32 LOD = InOutLODs[i];
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("LayingOut", "Laying out LOD {0}..."), LOD));
			FLayout Layout;
			FText Error;
			const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, LOD);
			const int32 NumChannels = MeshDescription ? FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels() : 0;
			if (!MeshDescription || !Fits(LOD, NumChannels) || !BuildLayout(*MeshDescription, SlotOrder, Settings, Layout, Error))
			{
				if (LOD == 0)
				{
					OutError = Error.IsEmpty() ? LOCTEXT("LOD0Failed", "LOD 0 could not be laid out.") : Error;
					return false;
				}
				OutSkipped.Add(LOD);
				InOutLODs.RemoveAt(i--);
				continue;
			}
			OutLayouts.Add(MoveTemp(Layout));
		}
		return true;
	}

	/** Appends the notes every apply shares: projected islands, skipped LODs, LODs laid out separately. */
	FText AddResultNotes(FText Message, const FUVLayoutPreview& Preview, const TArray<int32>& Skipped, int32 NumLODs)
	{
		if (Preview.NumProjected > 0)
		{
			Message = FText::Format(LOCTEXT("DoneProjected", "{0} {1} islands had no UV area and were laid flat from their 3D shape."), Message, Preview.NumProjected);
		}
		if (!Skipped.IsEmpty())
		{
			TArray<FString> Names;
			for (const int32 LOD : Skipped)
			{
				Names.Add(FString::FromInt(LOD));
			}
			Message = FText::Format(LOCTEXT("DoneSkipped", "{0} Skipped LOD {1} (missing the source channel or out of UV channels)."), Message, FText::FromString(FString::Join(Names, TEXT(", "))));
		}
		if (NumLODs > 1)
		{
			Message = FText::Format(LOCTEXT("DoneLODs", "{0} Each LOD is laid out from its own islands, so textures baked on LOD 0 may not line up exactly on the others."), Message);
		}
		return Message;
	}

	/** Writes one LOD's layout into Channel. */
	void WriteChannel(FMeshDescription& MeshDescription, int32 Channel, const TArray<FVector2f>& NewUVs)
	{
		TVertexInstanceAttributesRef<FVector2f> UVs = FStaticMeshAttributes(MeshDescription).GetVertexInstanceUVs();
		for (const FVertexInstanceID Instance : MeshDescription.VertexInstances().GetElementIDs())
		{
			UVs.Set(Instance, Channel, NewUVs.IsValidIndex(Instance.GetValue()) ? NewUVs[Instance.GetValue()] : FVector2f::ZeroVector);
		}
	}

	// TEXEL DENSITY

	/** One island of a channel as texel density sees it: its corners and how much UV it spends on how much surface. */
	struct FDensityIsland
	{
		/** Unique vertex instances (array indices). */
		TArray<int32> Instances;
		/** Its triangles, numbered in MeshDescription.Triangles() order. */
		TArray<int32> Triangles;
		double Area3D = 0.0;	// cm^2
		double AreaUV = 0.0;	// UV units^2
		FBox2f Bounds = FBox2f(ForceInit);

		/** UV units per cm, or 0 when the island has no UV or surface area. */
		float UVPerCm() const { return Area3D > 1e-6 && AreaUV > 1e-12 ? static_cast<float>(FMath::Sqrt(AreaUV / Area3D)) : 0.0f; }
	};

	/**
	 * The islands of Channel, joined the same way as BuildLayout step 1 (shared vertex instance, or a corner with
	 * the same position and UV), but across material slots: density doesn't care about materials.
	 */
	void FindDensityIslands(const FMeshDescription& MeshDescription, int32 Channel, TArray<FDensityIsland>& Out)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

		// 1. Triangle corners, in one flat list
		TArray<FVertexInstanceID> Corners;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			Corners.Append(MeshDescription.GetTriangleVertexInstances(TriangleID));
		}
		const int32 NumTriangles = Corners.Num() / 3;

		// 2. Join triangles that share a vertex instance, or a corner with the same position and (quantized) UV
		FUnionFind Sets(NumTriangles);
		TMap<int32, int32> TriangleOfInstance;
		TMap<FCornerKey, int32> TriangleOfKey;
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			for (int32 i = 0; i < 3; ++i)
			{
				const FVertexInstanceID Instance = Corners[Triangle * 3 + i];
				if (const int32* Other = TriangleOfInstance.Find(Instance.GetValue()))
				{
					Sets.Union(Triangle, *Other);
				}
				else
				{
					TriangleOfInstance.Add(Instance.GetValue(), Triangle);
				}
				const FVector2f UV = UVs.Get(Instance, Channel);
				const FCornerKey Key{
					MeshDescription.GetVertexInstanceVertex(Instance).GetValue(),
					FMath::RoundToInt32(FMath::Clamp(UV.X, -16000.0f, 16000.0f) * 65536.0f),
					FMath::RoundToInt32(FMath::Clamp(UV.Y, -16000.0f, 16000.0f) * 65536.0f),
					0 };
				if (const int32* Other = TriangleOfKey.Find(Key))
				{
					Sets.Union(Triangle, *Other);
				}
				else
				{
					TriangleOfKey.Add(Key, Triangle);
				}
			}
		}

		// 3. Per island: unique corners, UV bounds, and both areas
		TMap<int32, int32> IslandOfRoot;
		TSet<int32> Seen;
		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			const int32 Root = Sets.Find(Triangle);
			const int32* Found = IslandOfRoot.Find(Root);
			FDensityIsland& Island = Out[Found ? *Found : IslandOfRoot.Add(Root, Out.AddDefaulted())];
			Island.Triangles.Add(Triangle);

			FVector3f P[3];
			FVector2f T[3];
			for (int32 i = 0; i < 3; ++i)
			{
				const FVertexInstanceID Instance = Corners[Triangle * 3 + i];
				P[i] = Positions[MeshDescription.GetVertexInstanceVertex(Instance)];
				T[i] = UVs.Get(Instance, Channel);
				Island.Bounds += T[i];
				bool bAlreadySeen = false;
				Seen.Add(Instance.GetValue(), &bAlreadySeen);
				if (!bAlreadySeen)
				{
					Island.Instances.Add(Instance.GetValue());
				}
			}
			Island.Area3D += 0.5 * FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]).Size();
			Island.AreaUV += 0.5 * FMath::Abs(Cross2D(T[1] - T[0], T[2] - T[0]));
		}
	}

	/**
	 * New UVs for one LOD at TargetUVPerCm (indexed like the vertex instance array). Each island is scaled about its
	 * own bounds center, or with bPerIsland off, every island by the channel's one average factor about the
	 * channel's center. Islands with no UV area keep their UVs. Returns the number of those flat islands.
	 * OutIslands (optional) receives the islands found, for the preview.
	 */
	int32 ComputeDensityUVs(const FMeshDescription& MeshDescription, int32 Channel, float TargetUVPerCm, bool bPerIsland, TArray<FVector2f>& OutUVs,
		TArray<FDensityIsland>* OutIslands = nullptr)
	{
		// 1. Start from the current UVs, so anything not scaled stays exactly as it was
		TVertexInstanceAttributesConstRef<FVector2f> UVs = FStaticMeshConstAttributes(MeshDescription).GetVertexInstanceUVs();
		OutUVs.Init(FVector2f::ZeroVector, MeshDescription.VertexInstances().GetArraySize());
		for (const FVertexInstanceID Instance : MeshDescription.VertexInstances().GetElementIDs())
		{
			OutUVs[Instance.GetValue()] = UVs.Get(Instance, Channel);
		}

		TArray<FDensityIsland> Islands;
		FindDensityIslands(MeshDescription, Channel, Islands);

		// 2. Whole channel: one factor from the total areas, one pivot for everything
		float SharedFactor = 0.0f;
		FVector2f SharedPivot = FVector2f::ZeroVector;
		if (!bPerIsland)
		{
			FDensityIsland All;
			for (const FDensityIsland& Island : Islands)
			{
				All.Area3D += Island.Area3D;
				All.AreaUV += Island.AreaUV;
				All.Bounds += Island.Bounds;
			}
			const float Current = All.UVPerCm();
			SharedFactor = Current > 0.0f ? TargetUVPerCm / Current : 0.0f;
			SharedPivot = All.Bounds.GetCenter();
		}

		// 3. Scale each island's corners about the pivot: UV' = Pivot + (UV - Pivot) * Factor
		int32 NumFlat = 0;
		for (const FDensityIsland& Island : Islands)
		{
			const float Current = Island.UVPerCm();
			if (Current <= 0.0f)
			{
				++NumFlat;
				continue;
			}
			const float Factor = bPerIsland ? TargetUVPerCm / Current : SharedFactor;
			const FVector2f Pivot = bPerIsland ? Island.Bounds.GetCenter() : SharedPivot;
			if (Factor <= 0.0f)
			{
				continue;
			}
			for (const int32 Instance : Island.Instances)
			{
				OutUVs[Instance] = Pivot + (OutUVs[Instance] - Pivot) * Factor;
			}
		}
		if (OutIslands)
		{
			*OutIslands = MoveTemp(Islands);
		}
		return NumFlat;
	}
}

namespace UVLayout
{
	using namespace UVLayoutImpl;

	int32 GetMaxUVChannels(const UObject* Mesh)
	{
		return VrrCore::IsSkeletalMesh(Mesh) ? static_cast<int32>(MAX_TEXCOORDS) : static_cast<int32>(MAX_STATIC_TEXCOORDS);
	}

	int32 GetLightmapChannel(const UObject* Mesh)
	{
		const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		return StaticMesh ? StaticMesh->GetLightMapCoordinateIndex() : -1;
	}

	int32 GetNumMaterialSlots(UObject* Mesh)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			return 0;
		}
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		TSet<FName> Slots;
		for (const FPolygonGroupID Group : MeshDescription->PolygonGroups().GetElementIDs())
		{
			if (MeshDescription->GetNumPolygonGroupPolygons(Group) > 0)
			{
				const FName Slot = SlotNames.IsValid() ? SlotNames[Group] : NAME_None;
				Slots.Add(Slot.IsNone() ? FName(*FString::Printf(TEXT("Section %d"), Group.GetValue())) : Slot);
			}
		}
		return Slots.Num();
	}

	FUVLayoutChannelPlan PlanChannels(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		FUVLayoutChannelPlan Plan;
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			Plan.Blocker = LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data for LOD 0.");
			return Plan;
		}
		const int32 NumChannels = FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels();
		const int32 MaxChannels = GetMaxUVChannels(Mesh);
		const int32 Source = Settings.SourceChannel;
		const int32 Target = Settings.TargetChannel;

		if (Source < 0 || Source >= NumChannels)
		{
			Plan.Blocker = FText::Format(LOCTEXT("PlanNoSource", "The mesh has no UV{0}."), Source);
			return Plan;
		}
		if (Target == Source)
		{
			Plan.Blocker = LOCTEXT("PlanSame", "Pick a target channel different from the source.");
			return Plan;
		}
		if (Target < 0 || Target > NumChannels)
		{
			Plan.Blocker = FText::Format(LOCTEXT("PlanGap", "Pick UV{0} or lower (the next free channel)."), NumChannels);
			return Plan;
		}

		const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		Plan.bMovesLightmap = IsLightmapChannel(StaticMesh, 0, Target);
		const int32 FinalChannels = Plan.bMovesLightmap ? FMath::Max(NumChannels, Target) + 1 : FMath::Max(NumChannels, Target + 1);
		if (FinalChannels > MaxChannels)
		{
			Plan.Blocker = FText::Format(LOCTEXT("PlanTooMany", "That would need {0} UV channels; this mesh type supports {1}."), FinalChannels, MaxChannels);
			return Plan;
		}

		if (Plan.bMovesLightmap)
		{
			const bool bGenerated = StaticMesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs;
			Plan.Summary = FText::Format(LOCTEXT("PlanMove", "UV{0} holds the lightmap. It moves to UV{1} (lightmap index{2} updated), and the new layout goes into UV{0}.{3}"),
				Target, Target + 1,
				bGenerated ? LOCTEXT("PlanMoveGenerated", " and Generate Lightmap UVs target") : FText::GetEmpty(),
				Target + 1 < NumChannels ? FText::Format(LOCTEXT("PlanShift", " UV{0} and up shift up one channel too."), Target + 1) : FText::GetEmpty());
		}
		else if (Target < NumChannels)
		{
			Plan.bOverwrites = true;
			const FMeshBuildSettings* Build = StaticMesh ? &StaticMesh->GetSourceModel(0).BuildSettings : nullptr;
			const bool bLightmapSource = Build && Build->bGenerateLightmapUVs && Build->SrcLightmapIndex == Target;
			Plan.Summary = FText::Format(LOCTEXT("PlanOverwrite", "UV{0} already exists: its UVs are replaced by the new layout.{1}"), Target,
				bLightmapSource ? LOCTEXT("PlanLightmapSource", " Generated lightmap UVs are built from this channel, so they will change too.") : FText::GetEmpty());
		}
		else
		{
			Plan.Summary = FText::Format(LOCTEXT("PlanNew", "Adds UV{0} with the new layout."), Target);
		}
		return Plan;
	}

	bool ComputePreview(UObject* Mesh, const FUVLayoutSettings& Settings, FUVLayoutPreview& OutPreview, FText& OutError)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			OutError = LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data for LOD 0.");
			return false;
		}
		FLayout Layout;
		if (!BuildLayout(*MeshDescription, GetSlotOrder(Mesh), Settings, Layout, OutError))
		{
			return false;
		}
		OutPreview = MoveTemp(Layout.Preview);
		return true;
	}

	FUVLayoutResult Apply(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		FUVLayoutResult Result;
		const FUVLayoutChannelPlan Plan = PlanChannels(Mesh, Settings);
		if (!Plan.Blocker.IsEmpty())
		{
			Result.Message = Plan.Blocker;
			return Result;
		}

		TArray<int32> LODs = Settings.bAllLODs ? VrrCore::GetLODsWithMeshData(Mesh) : TArray<int32>{ 0 };
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);
		const int32 Target = Settings.TargetChannel;
		const int32 MaxChannels = GetMaxUVChannels(Mesh);

		FScopedSlowTask SlowTask(static_cast<float>(LODs.Num() * 2 + 1), LOCTEXT("Applying", "Creating UV layout..."));
		SlowTask.MakeDialog();

		// 1. Lay out every LOD before touching anything, so a failure leaves the mesh as it was
		TArray<FLayout> Layouts;
		TArray<int32> Skipped;
		auto Fits = [&](int32 LOD, int32 NumChannels)
		{
			return (IsLightmapChannel(StaticMesh, LOD, Target) ? FMath::Max(NumChannels, Target) + 1 : FMath::Max(NumChannels, Target + 1)) <= MaxChannels;
		};
		if (!LayOutLODs(Mesh, Settings, Fits, LODs, Layouts, Skipped, SlowTask, Result.Message))
		{
			return Result;
		}

		const FScopedTransaction Transaction(LOCTEXT("Transaction", "Create UV Layout"));
		{
			// Skeletal meshes rebuild (and their components re-register) once this scope ends
			TUniquePtr<FScopedSkeletalMeshPostEditChange> SkeletalScope;
			if (SkeletalMesh)
			{
				SkeletalScope = MakeUnique<FScopedSkeletalMeshPostEditChange>(SkeletalMesh);
			}
			Mesh->Modify();

			// The lightmap index is mesh-wide: decide once, from LOD 0
			const bool bMoveLightmapIndex = StaticMesh && StaticMesh->GetLightMapCoordinateIndex() == Target;

			for (int32 i = 0; i < LODs.Num(); ++i)
			{
				const int32 LOD = LODs[i];
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Writing", "Writing LOD {0}..."), LOD));
				const bool bLightmapHere = IsLightmapChannel(StaticMesh, LOD, Target);
				FMeshBuildSettings* Build = StaticMesh ? &StaticMesh->GetSourceModel(LOD).BuildSettings : nullptr;

				ModifyMeshDescription(Mesh, LOD);
				FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, LOD);
				int32 NumChannels = FStaticMeshAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels();

				// 2. Make room: move a lightmap out of the target channel, or add channels up to it
				if (Target < NumChannels)
				{
					if (bLightmapHere)
					{
						FStaticMeshOperations::InsertUVChannel(*MeshDescription, Target);
						if (Build)
						{
							Build->SrcLightmapIndex += Build->SrcLightmapIndex >= Target ? 1 : 0;
							Build->DstLightmapIndex += Build->DstLightmapIndex >= Target ? 1 : 0;
						}
					}
				}
				else
				{
					while (NumChannels <= Target)
					{
						FStaticMeshOperations::InsertUVChannel(*MeshDescription, NumChannels++);
					}
					// Generate Lightmap UVs wrote into a channel that only exists after the build: point it past ours
					if (bLightmapHere && Build && Build->bGenerateLightmapUVs && Build->DstLightmapIndex == Target)
					{
						Build->DstLightmapIndex = Target + 1;
					}
				}

				// 3. Write the layout
				WriteChannel(*MeshDescription, Target, Layouts[i].UVs);
				CommitMeshDescription(Mesh, LOD);
			}

			if (bMoveLightmapIndex)
			{
				StaticMesh->SetLightMapCoordinateIndex(Target + 1);
			}

			// 4. Rebuild (skeletal meshes rebuild when SkeletalScope ends)
			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Rebuilding", "Rebuilding mesh..."));
			if (StaticMesh)
			{
				StaticMesh->PostEditChange();
			}
		}

		// 5. Summary
		const FUVLayoutPreview& Preview = Layouts[0].Preview;
		Result.bSuccess = true;
		Result.Message = FText::Format(LOCTEXT("Done", "Wrote UV{0} on {1} {1}|plural(one=LOD,other=LODs): {2} islands in {3} {3}|plural(one=region,other=regions), {4} of the square covered.{5}"),
			Target, LODs.Num(), Preview.NumIslands, Preview.Regions.Num(), FText::AsPercent(Preview.Coverage),
			Plan.bMovesLightmap ? FText::Format(LOCTEXT("DoneMoved", " Lightmap moved to UV{0}."), Target + 1) : FText::GetEmpty());
		Result.Message = AddResultNotes(Result.Message, Preview, Skipped, LODs.Num());
		return Result;
	}

	// BALANCED UV LAYOUT

	bool SupportsLightmaps(const UObject* Mesh)
	{
		return Cast<UStaticMesh>(Mesh) != nullptr;
	}

	bool IsBalancedLightmap(const UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		return Settings.bBalancedAsLightmap && SupportsLightmaps(Mesh);
	}

	FUVLayoutSettings MakeBalancedSettings(const FUVLayoutSettings& Settings)
	{
		FUVLayoutSettings Balanced = Settings;
		Balanced.bGroupByMaterial = false;
		Balanced.bMatchDensityAcrossMaterials = false;
		Balanced.bGroupAsUDIMs = false;
		Balanced.Padding = Settings.BalancedPadding;
		Balanced.PaddingResolution = Settings.BalancedResolution;
		Balanced.TargetChannel = Settings.BalancedTargetChannel;
		Balanced.PackMethod = Settings.BalancedPackMethod;
		Balanced.PackDetail = Settings.BalancedPackDetail;
		Balanced.bStretchToFill = Settings.bBalancedStretchToFill;
		return Balanced;
	}

	FUVLayoutBalancedPlan PlanBalanced(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		if (IsBalancedLightmap(Mesh, Settings))
		{
			return PlanLightmap(Mesh, Settings);
		}
		const FUVLayoutChannelPlan ChannelPlan = PlanChannels(Mesh, MakeBalancedSettings(Settings));
		FUVLayoutBalancedPlan Plan;
		Plan.Blocker = ChannelPlan.Blocker;
		Plan.Summary = ChannelPlan.Summary;
		Plan.Channel = Settings.BalancedTargetChannel;
		return Plan;
	}

	FUVLayoutResult ApplyBalanced(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		return IsBalancedLightmap(Mesh, Settings) ? ApplyLightmap(Mesh, Settings) : Apply(Mesh, MakeBalancedSettings(Settings));
	}

	FUVLayoutBalancedPlan PlanLightmap(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		FUVLayoutBalancedPlan Plan;
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!StaticMesh)
		{
			Plan.Blocker = LOCTEXT("LightmapStaticOnly", "Only static meshes have lightmaps.");
			return Plan;
		}
		if (!MeshDescription)
		{
			Plan.Blocker = LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data for LOD 0.");
			return Plan;
		}
		const int32 NumChannels = FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels();
		const int32 Source = Settings.SourceChannel;
		if (Source < 0 || Source >= NumChannels)
		{
			Plan.Blocker = FText::Format(LOCTEXT("PlanNoSource", "The mesh has no UV{0}."), Source);
			return Plan;
		}

		// Replace what Unreal generates, else the channel the lightmap already reads, else add one at the end
		const FMeshBuildSettings& Build = StaticMesh->GetSourceModel(0).BuildSettings;
		const int32 Index = StaticMesh->GetLightMapCoordinateIndex();
		FText Reason;
		if (Build.bGenerateLightmapUVs && Build.DstLightmapIndex != Source)
		{
			Plan.Channel = Build.DstLightmapIndex;
			Reason = FText::Format(LOCTEXT("ReasonGenerated", "replaces Unreal's generated lightmap UVs in UV{0} (Generate Lightmap UVs is turned off so the build keeps them)"), Plan.Channel);
		}
		else if (Index > 0 && Index != Source && Index < NumChannels)
		{
			Plan.Channel = Index;
			Reason = FText::Format(LOCTEXT("ReasonExisting", "replaces the lightmap UVs in UV{0}"), Plan.Channel);
		}
		else
		{
			Plan.Channel = NumChannels;
			Reason = FText::Format(LOCTEXT("ReasonNew", "go into a new UV{0}"), Plan.Channel);
		}

		if (Plan.Channel >= GetMaxUVChannels(Mesh))
		{
			Plan.Blocker = FText::Format(LOCTEXT("LightmapTooMany", "The lightmap would need UV{0}; static meshes support up to UV{1}."), Plan.Channel, GetMaxUVChannels(Mesh) - 1);
			return Plan;
		}
		Plan.Summary = FText::Format(LOCTEXT("LightmapPlan", "Lightmap UVs from UV{0} {1}. Light Map Coordinate Index becomes {2} and Light Map Resolution {3}."),
			Source, Reason, Plan.Channel, Settings.BalancedResolution);
		return Plan;
	}

	FUVLayoutResult ApplyLightmap(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		FUVLayoutResult Result;
		const FUVLayoutBalancedPlan Plan = PlanLightmap(Mesh, Settings);
		if (!Plan.Blocker.IsEmpty())
		{
			Result.Message = Plan.Blocker;
			return Result;
		}
		UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Mesh);
		const FUVLayoutSettings LayoutSettings = MakeBalancedSettings(Settings);
		const int32 Channel = Plan.Channel;
		const int32 MaxChannels = GetMaxUVChannels(Mesh);
		TArray<int32> LODs = Settings.bAllLODs ? VrrCore::GetLODsWithMeshData(Mesh) : TArray<int32>{ 0 };

		FScopedSlowTask SlowTask(static_cast<float>(LODs.Num() * 2 + 1), LOCTEXT("ApplyingLightmap", "Creating lightmap UVs..."));
		SlowTask.MakeDialog();

		// 1. Lay out every LOD first
		TArray<FLayout> Layouts;
		TArray<int32> Skipped;
		auto Fits = [&](int32, int32 NumChannels) { return FMath::Max(NumChannels, Channel + 1) <= MaxChannels; };
		if (!LayOutLODs(Mesh, LayoutSettings, Fits, LODs, Layouts, Skipped, SlowTask, Result.Message))
		{
			return Result;
		}

		const FScopedTransaction Transaction(LOCTEXT("LightmapTransaction", "Create Lightmap UVs"));
		{
			Mesh->Modify();
			for (int32 i = 0; i < LODs.Num(); ++i)
			{
				const int32 LOD = LODs[i];
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Writing", "Writing LOD {0}..."), LOD));

				ModifyMeshDescription(Mesh, LOD);
				FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, LOD);

				// 2. Make sure the channel exists (a generated lightmap channel only exists after the build)
				int32 NumChannels = FStaticMeshAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels();
				while (NumChannels <= Channel)
				{
					FStaticMeshOperations::InsertUVChannel(*MeshDescription, NumChannels++);
				}

				// 3. Write, and stop the build from generating over it
				WriteChannel(*MeshDescription, Channel, Layouts[i].UVs);
				StaticMesh->GetSourceModel(LOD).BuildSettings.bGenerateLightmapUVs = false;
				CommitMeshDescription(Mesh, LOD);
			}

			// 4. Point the lightmap at it, with the resolution the padding was measured for
			StaticMesh->SetLightMapCoordinateIndex(Channel);
			StaticMesh->SetLightMapResolution(Settings.BalancedResolution);

			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Rebuilding", "Rebuilding mesh..."));
			StaticMesh->PostEditChange();
		}

		const FUVLayoutPreview& Preview = Layouts[0].Preview;
		Result.bSuccess = true;
		Result.Message = FText::Format(LOCTEXT("LightmapDone", "Wrote lightmap UVs into UV{0} on {1} {1}|plural(one=LOD,other=LODs): {2} islands, {3} of the square covered, {4} texel padding at {5}. Generate Lightmap UVs is now off."),
			Channel, LODs.Num(), Preview.NumIslands, FText::AsPercent(Preview.Coverage), Settings.BalancedPadding, Settings.BalancedResolution);
		Result.Message = AddResultNotes(Result.Message, Preview, Skipped, LODs.Num());
		return Result;
	}

	// TEXEL DENSITY

	float GetTargetUVPerCm(const FUVLayoutSettings& Settings)
	{
		// One UV tile covers DensityMeters: 1 / (meters * 100) UV units per cm
		return 1.0f / (FMath::Max(Settings.DensityMeters, 0.001f) * 100.0f);
	}

	FUVDensityInfo MeasureDensity(UObject* Mesh, int32 Channel)
	{
		FUVDensityInfo Info;
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription || Channel < 0 || Channel >= FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels())
		{
			return Info;
		}

		TArray<FDensityIsland> Islands;
		FindDensityIslands(*MeshDescription, Channel, Islands);

		// Average over the whole channel (area weighted), and the spread between islands
		double Area3D = 0.0;
		double AreaUV = 0.0;
		Info.MinUVPerCm = UE_BIG_NUMBER;
		for (const FDensityIsland& Island : Islands)
		{
			Area3D += Island.Area3D;
			AreaUV += Island.AreaUV;
			const float Density = Island.UVPerCm();
			if (Density <= 0.0f)
			{
				++Info.NumFlat;
				continue;
			}
			Info.MinUVPerCm = FMath::Min(Info.MinUVPerCm, Density);
			Info.MaxUVPerCm = FMath::Max(Info.MaxUVPerCm, Density);
		}
		Info.NumIslands = Islands.Num();
		Info.AverageUVPerCm = Area3D > 1e-6 ? static_cast<float>(FMath::Sqrt(AreaUV / Area3D)) : 0.0f;
		Info.bValid = Info.AverageUVPerCm > 0.0f;
		if (!Info.bValid)
		{
			Info.MinUVPerCm = 0.0f;
		}
		return Info;
	}

	bool ComputeDensityPreview(UObject* Mesh, const FUVLayoutSettings& Settings, FUVLayoutPreview& OutPreview, FText& OutError)
	{
		OutError = GetDensityBlocker(Mesh, Settings);
		if (!OutError.IsEmpty())
		{
			return false;
		}
		const FMeshDescription& MeshDescription = *VrrCore::GetMeshDescription(Mesh, 0);

		// 1. The scaled UVs and the islands they were scaled by
		TArray<FVector2f> UVs;
		TArray<FDensityIsland> Islands;
		OutPreview = FUVLayoutPreview();
		OutPreview.bTiled = true;
		OutPreview.NumProjected = ComputeDensityUVs(MeshDescription, Settings.DensityChannel, GetTargetUVPerCm(Settings), Settings.bDensityPerIsland, UVs, &Islands);
		OutPreview.NumIslands = Islands.Num();

		// 2. Triangles in island order, colored per island (the preview cycles its palette by group number)
		TArray<FVertexInstanceID> Corners;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			Corners.Append(MeshDescription.GetTriangleVertexInstances(TriangleID));
		}
		FBox2f Bounds(FVector2f(0.0f), FVector2f(1.0f));	// always show the 0-1 tile
		for (int32 Island = 0; Island < Islands.Num(); ++Island)
		{
			// 3. Outline: edges (by position, so hard-normal splits don't count) used by one triangle of the island
			TMap<uint64, int32> EdgeUses;
			auto EdgeKey = [&](int32 Triangle, int32 i)
			{
				uint32 A = static_cast<uint32>(MeshDescription.GetVertexInstanceVertex(Corners[Triangle * 3 + i]).GetValue());
				uint32 B = static_cast<uint32>(MeshDescription.GetVertexInstanceVertex(Corners[Triangle * 3 + (i + 1) % 3]).GetValue());
				if (A > B)
				{
					Swap(A, B);
				}
				return (static_cast<uint64>(A) << 32) | B;
			};
			for (const int32 Triangle : Islands[Island].Triangles)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					const FVector2f UV = UVs[Corners[Triangle * 3 + i].GetValue()];
					OutPreview.Triangles.Add(UV);
					Bounds += UV;
					EdgeUses.FindOrAdd(EdgeKey(Triangle, i))++;
				}
				OutPreview.TriangleGroups.Add(Island);
			}
			for (const int32 Triangle : Islands[Island].Triangles)
			{
				for (int32 i = 0; i < 3; ++i)
				{
					if (EdgeUses.FindChecked(EdgeKey(Triangle, i)) == 1)
					{
						OutPreview.Outlines.Add(UVs[Corners[Triangle * 3 + i].GetValue()]);
						OutPreview.Outlines.Add(UVs[Corners[Triangle * 3 + (i + 1) % 3].GetValue()]);
					}
				}
			}
		}

		// 4. The view: every whole tile the UVs touch
		OutPreview.Regions.Add(FBox2f(
			FVector2f(FMath::FloorToFloat(Bounds.Min.X), FMath::FloorToFloat(Bounds.Min.Y)),
			FVector2f(FMath::CeilToFloat(Bounds.Max.X), FMath::CeilToFloat(Bounds.Max.Y))));
		OutPreview.RegionNames.Add(NAME_None);
		return true;
	}

	FText GetDensityBlocker(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			return LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data for LOD 0.");
		}
		const int32 Channel = Settings.DensityChannel;
		if (Channel < 0 || Channel >= FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels())
		{
			return FText::Format(LOCTEXT("PlanNoSource", "The mesh has no UV{0}."), Channel);
		}
		// Lightmaps must stay inside 0-1, so a real-world scale would break them
		if (IsLightmapChannel(Cast<UStaticMesh>(Mesh), 0, Channel))
		{
			return FText::Format(LOCTEXT("DensityLightmap", "UV{0} is the lightmap channel, which must stay inside 0-1. Pick the texturing channel (usually UV0)."), Channel);
		}
		return FText::GetEmpty();
	}

	FUVLayoutResult ApplyTexelDensity(UObject* Mesh, const FUVLayoutSettings& Settings)
	{
		FUVLayoutResult Result;
		Result.Message = GetDensityBlocker(Mesh, Settings);
		if (!Result.Message.IsEmpty())
		{
			return Result;
		}
		const int32 Channel = Settings.DensityChannel;
		const float Target = GetTargetUVPerCm(Settings);
		TArray<int32> LODs = Settings.bAllLODs ? VrrCore::GetLODsWithMeshData(Mesh) : TArray<int32>{ 0 };
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);

		FScopedSlowTask SlowTask(static_cast<float>(LODs.Num() * 2 + 1), LOCTEXT("ApplyingDensity", "Scaling UVs to texel density..."));
		SlowTask.MakeDialog();

		// 1. Compute every LOD first (LODs without the channel are skipped), so nothing changes if one fails
		TArray<TArray<FVector2f>> NewUVs;
		TArray<int32> Skipped;
		int32 NumFlat = 0;
		for (int32 i = 0; i < LODs.Num(); ++i)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Measuring", "Measuring LOD {0}..."), LODs[i]));
			const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, LODs[i]);
			if (!MeshDescription || Channel >= FStaticMeshConstAttributes(*MeshDescription).GetVertexInstanceUVs().GetNumChannels())
			{
				Skipped.Add(LODs[i]);
				LODs.RemoveAt(i--);
				continue;
			}
			const int32 Flat = ComputeDensityUVs(*MeshDescription, Channel, Target, Settings.bDensityPerIsland, NewUVs.AddDefaulted_GetRef());
			NumFlat += LODs[i] == 0 ? Flat : 0;
		}

		// 2. Write them (undoable); skeletal meshes rebuild when SkeletalScope ends
		const FScopedTransaction Transaction(LOCTEXT("DensityTransaction", "Scale UVs to Texel Density"));
		{
			TUniquePtr<FScopedSkeletalMeshPostEditChange> SkeletalScope;
			if (SkeletalMesh)
			{
				SkeletalScope = MakeUnique<FScopedSkeletalMeshPostEditChange>(SkeletalMesh);
			}
			Mesh->Modify();
			for (int32 i = 0; i < LODs.Num(); ++i)
			{
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Writing", "Writing LOD {0}..."), LODs[i]));
				ModifyMeshDescription(Mesh, LODs[i]);
				WriteChannel(*VrrCore::GetMeshDescription(Mesh, LODs[i]), Channel, NewUVs[i]);
				CommitMeshDescription(Mesh, LODs[i]);
			}
			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Rebuilding", "Rebuilding mesh..."));
			if (StaticMesh)
			{
				StaticMesh->PostEditChange();
			}
		}

		// 3. Summary
		Result.bSuccess = true;
		Result.Message = FText::Format(LOCTEXT("DensityDone", "Scaled UV{0} on {1} {1}|plural(one=LOD,other=LODs) to {2} px/m (one tile = {3} m at {4} px){5}."),
			Channel, LODs.Num(), FText::AsNumber(FMath::RoundToInt32(Target * Settings.DensityTextureSize * 100.0f)),
			FText::AsNumber(Settings.DensityMeters), Settings.DensityTextureSize,
			Settings.bDensityPerIsland ? LOCTEXT("DensityEach", ", each island evened out") : LOCTEXT("DensityWhole", ", whole channel by one factor"));
		if (NumFlat > 0)
		{
			Result.Message = FText::Format(LOCTEXT("DensityFlat", "{0} {1} islands had no UV area and were left as they were."), Result.Message, NumFlat);
		}
		if (!Skipped.IsEmpty())
		{
			TArray<FString> Names;
			for (const int32 LOD : Skipped)
			{
				Names.Add(FString::FromInt(LOD));
			}
			Result.Message = FText::Format(LOCTEXT("DensitySkipped", "{0} Skipped LOD {1} (no UV{2})."), Result.Message, FText::FromString(FString::Join(Names, TEXT(", "))), Channel);
		}
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
