// Copyright Gradientspace Corp. All Rights Reserved.
#include "GridIO/MagicaVoxReader.h"

#include <filesystem>
#include <stdio.h>


using namespace GS;


/*
 * Reader for Magica Voxel .vox file format
 * https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt
 */


struct VoxChunkHeader
{
	char ChunkID[4];
	uint32_t ChunkBytes;
	uint32_t ChildrenChunksBytes;

	bool IsChunkID(const char IDString[4]) const
	{
		return ChunkID[0] == IDString[0] && ChunkID[1] == IDString[1] && ChunkID[2] == IDString[2] && ChunkID[3] == IDString[3];
	}
};

struct VoxSizeChunkData
{
	uint32_t SizeX;
	uint32_t SizeY;
	uint32_t SizeZ;
};

struct VoxVoxel
{
	uint8_t X;
	uint8_t Y;
	uint8_t Z;
	uint8_t ColorIndex;
};

struct VoxChunk
{
	VoxSizeChunkData Size;
	uint32_t NumVoxels;
	std::vector<VoxVoxel> Voxels;
};

struct VOXDict
{
	uint32_t NumKeyValuePairs;
	std::vector<std::string> Keys;
	std::vector<std::string> Values;
	const std::string* FindValue(const std::string& key) const {
		for (int k = 0; k < Keys.size(); ++k)
			if (Keys[k] == key) 
				return &Values[k];
		return nullptr;
	}
	static int ParseInt(const std::string& Value) {
		return atoi(Value.c_str());
	}
	static bool ParseInt3(const std::string& Value, int& X, int& Y, int& Z) {
		size_t first_space = Value.find_first_of(' ');
		size_t second_space = Value.find_last_of(' ');
		if (first_space == std::string::npos || second_space == std::string::npos)
			return false;
		X = atoi(Value.c_str());
		Y = atoi(&Value[first_space]);
		Z = atoi(&Value[second_space]);
		return true;
	}
	int FindIntValue(const std::string& Key, int defaultValue) const
	{
		if (const std::string* FoundStr = FindValue(Key))
			return ParseInt(*FoundStr);
		return defaultValue;
	}
	bool FindInt3Value(const std::string& Key, int& X, int& Y, int& Z) const
	{
		if (const std::string* FoundStr = FindValue(Key))
			return ParseInt3(*FoundStr, X, Y, Z);
		return false;
	}
};

struct VOXTransformFrame
{
	VOXDict FrameAttributes;
	uint8_t	Rotation = 0;
	GS::Vector3i Translation = GS::Vector3i::Zero();
	int32_t FrameIndex = 0;
};

struct VOXTransform
{
	int32_t NodeID;
	VOXDict NodeAttributes;
	int32_t ChildNodeID;
	int32_t ReservedID;			// always -1
	int32_t LayerID;
	int32_t NumFrames;			// > 0
	std::vector<VOXTransformFrame> Frames;
};

struct VOXGroup
{
	int32_t NodeID;
	VOXDict NodeAttributes;
	std::vector<int32_t> Children;
};

struct VOXShape
{
	int32_t NodeID;
	VOXDict NodeAttributes;
	int32_t NumModels;
	std::vector<int32_t> ModelIDs;
	std::vector<VOXDict> ModelAttributes;
};

struct VoxColor
{
	union {
		struct {
			uint8_t R;
			uint8_t G;
			uint8_t B;
			uint8_t A;
		};
		uint32_t Encoded;
	};
	VoxColor(uint32_t EncodedColor) { Encoded = EncodedColor; }
};

struct VoxColorPalette
{
	uint32_t Palette[256];

	VoxColor GetColor(int ColorIndex) const
	{
		return VoxColor(Palette[ColorIndex]);
	}

	static VoxColorPalette StandardPalette()
	{
		return {
			0x00000000, 0xffffffff, 0xffccffff, 0xff99ffff, 0xff66ffff, 0xff33ffff, 0xff00ffff, 0xffffccff, 0xffccccff, 0xff99ccff, 0xff66ccff, 0xff33ccff, 0xff00ccff, 0xffff99ff, 0xffcc99ff, 0xff9999ff,
			0xff6699ff, 0xff3399ff, 0xff0099ff, 0xffff66ff, 0xffcc66ff, 0xff9966ff, 0xff6666ff, 0xff3366ff, 0xff0066ff, 0xffff33ff, 0xffcc33ff, 0xff9933ff, 0xff6633ff, 0xff3333ff, 0xff0033ff, 0xffff00ff,
			0xffcc00ff, 0xff9900ff, 0xff6600ff, 0xff3300ff, 0xff0000ff, 0xffffffcc, 0xffccffcc, 0xff99ffcc, 0xff66ffcc, 0xff33ffcc, 0xff00ffcc, 0xffffcccc, 0xffcccccc, 0xff99cccc, 0xff66cccc, 0xff33cccc,
			0xff00cccc, 0xffff99cc, 0xffcc99cc, 0xff9999cc, 0xff6699cc, 0xff3399cc, 0xff0099cc, 0xffff66cc, 0xffcc66cc, 0xff9966cc, 0xff6666cc, 0xff3366cc, 0xff0066cc, 0xffff33cc, 0xffcc33cc, 0xff9933cc,
			0xff6633cc, 0xff3333cc, 0xff0033cc, 0xffff00cc, 0xffcc00cc, 0xff9900cc, 0xff6600cc, 0xff3300cc, 0xff0000cc, 0xffffff99, 0xffccff99, 0xff99ff99, 0xff66ff99, 0xff33ff99, 0xff00ff99, 0xffffcc99,
			0xffcccc99, 0xff99cc99, 0xff66cc99, 0xff33cc99, 0xff00cc99, 0xffff9999, 0xffcc9999, 0xff999999, 0xff669999, 0xff339999, 0xff009999, 0xffff6699, 0xffcc6699, 0xff996699, 0xff666699, 0xff336699,
			0xff006699, 0xffff3399, 0xffcc3399, 0xff993399, 0xff663399, 0xff333399, 0xff003399, 0xffff0099, 0xffcc0099, 0xff990099, 0xff660099, 0xff330099, 0xff000099, 0xffffff66, 0xffccff66, 0xff99ff66,
			0xff66ff66, 0xff33ff66, 0xff00ff66, 0xffffcc66, 0xffcccc66, 0xff99cc66, 0xff66cc66, 0xff33cc66, 0xff00cc66, 0xffff9966, 0xffcc9966, 0xff999966, 0xff669966, 0xff339966, 0xff009966, 0xffff6666,
			0xffcc6666, 0xff996666, 0xff666666, 0xff336666, 0xff006666, 0xffff3366, 0xffcc3366, 0xff993366, 0xff663366, 0xff333366, 0xff003366, 0xffff0066, 0xffcc0066, 0xff990066, 0xff660066, 0xff330066,
			0xff000066, 0xffffff33, 0xffccff33, 0xff99ff33, 0xff66ff33, 0xff33ff33, 0xff00ff33, 0xffffcc33, 0xffcccc33, 0xff99cc33, 0xff66cc33, 0xff33cc33, 0xff00cc33, 0xffff9933, 0xffcc9933, 0xff999933,
			0xff669933, 0xff339933, 0xff009933, 0xffff6633, 0xffcc6633, 0xff996633, 0xff666633, 0xff336633, 0xff006633, 0xffff3333, 0xffcc3333, 0xff993333, 0xff663333, 0xff333333, 0xff003333, 0xffff0033,
			0xffcc0033, 0xff990033, 0xff660033, 0xff330033, 0xff000033, 0xffffff00, 0xffccff00, 0xff99ff00, 0xff66ff00, 0xff33ff00, 0xff00ff00, 0xffffcc00, 0xffcccc00, 0xff99cc00, 0xff66cc00, 0xff33cc00,
			0xff00cc00, 0xffff9900, 0xffcc9900, 0xff999900, 0xff669900, 0xff339900, 0xff009900, 0xffff6600, 0xffcc6600, 0xff996600, 0xff666600, 0xff336600, 0xff006600, 0xffff3300, 0xffcc3300, 0xff993300,
			0xff663300, 0xff333300, 0xff003300, 0xffff0000, 0xffcc0000, 0xff990000, 0xff660000, 0xff330000, 0xff0000ee, 0xff0000dd, 0xff0000bb, 0xff0000aa, 0xff000088, 0xff000077, 0xff000055, 0xff000044,
			0xff000022, 0xff000011, 0xff00ee00, 0xff00dd00, 0xff00bb00, 0xff00aa00, 0xff008800, 0xff007700, 0xff005500, 0xff004400, 0xff002200, 0xff001100, 0xffee0000, 0xffdd0000, 0xffbb0000, 0xffaa0000,
			0xff880000, 0xff770000, 0xff550000, 0xff440000, 0xff220000, 0xff110000, 0xffeeeeee, 0xffdddddd, 0xffbbbbbb, 0xffaaaaaa, 0xff888888, 0xff777777, 0xff555555, 0xff444444, 0xff222222, 0xff111111
		};
	}
};



template<int NumBytes>
struct BinaryHeader
{
	char Text[NumBytes];

	size_t Read(const char* Buffer, size_t BufferSize, size_t Offset)
	{
		if (Offset + NumBytes >= BufferSize) return 0;
		for (int j = 0; j < NumBytes; ++j)
			Text[j] = Buffer[Offset+j];
		return NumBytes;
	}

	bool BeginsWith(const char* String, size_t NumChars) const
	{
		if (NumChars > NumBytes) return false;
		for (size_t j = 0; j < NumChars; ++j)
			if (Text[j] != String[j])
				return false;
		return true;
	}
};


template<typename DataType>
bool ReadTypeAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, DataType& DataOut)
{
	size_t DataSize = sizeof(DataType);
	if (OffsetInOut + DataSize >= BufferSize) return false;

	errno_t error = memcpy_s((void*)&DataOut, DataSize, (const void*)(Buffer+OffsetInOut), DataSize);
	if (error != 0) return false;

	OffsetInOut += DataSize;
	return true;
}

template<typename DataType>
bool ReadTypeArrayAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, size_t NumElementsToRead, DataType* DataArrayOut)
{
	size_t DataSize = sizeof(DataType);
	if (OffsetInOut + (DataSize*NumElementsToRead) >= BufferSize) return false;

	errno_t error = memcpy_s((void*)DataArrayOut, DataSize*NumElementsToRead, (const void*)(Buffer + OffsetInOut), DataSize*NumElementsToRead);
	if (error != 0) return false;

	OffsetInOut += DataSize*NumElementsToRead;
	return true;
}

static bool ReadStringAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, std::string& stringOut)
{
	uint32_t BufferSizeInBytes;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, BufferSizeInBytes))
		return false;

	if (BufferSizeInBytes == 0) {
		stringOut = std::string();
		return true;
	}

	dynamic_buffer<char> stringbuf; stringbuf.resize(BufferSizeInBytes + 1);
	if (!ReadTypeArrayAndIncrement<char>(Buffer, BufferSize, OffsetInOut, BufferSizeInBytes, &stringbuf[0]))
		return false;
	stringbuf.last() = '\0';
	stringOut = std::string(&stringbuf[0]);
	return true;
}


static bool ReadDictAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, VOXDict& dictOut)
{
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, dictOut.NumKeyValuePairs))
		return false;

	if (dictOut.NumKeyValuePairs == 0)
		return true;

	dictOut.Keys.resize(dictOut.NumKeyValuePairs);
	dictOut.Values.resize(dictOut.NumKeyValuePairs);
	for (uint32_t k = 0; k < dictOut.NumKeyValuePairs; ++k)
	{
		std::string key, value;
		bool bKeyOK = ReadStringAndIncrement(Buffer, BufferSize, OffsetInOut, key);
		bool bValueOK = ReadStringAndIncrement(Buffer, BufferSize, OffsetInOut, value);
		if (!(bKeyOK && bValueOK))
			return false;
		dictOut.Keys[k] = std::move(key);
		dictOut.Values[k] = std::move(value);
	}
	return true;
}


static bool ReadTransformAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, VOXTransform& transformOut)
{
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.NodeID))
		return false;
	if (!ReadDictAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.NodeAttributes))
		return false;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.ChildNodeID))
		return false;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.ReservedID))
		return false;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.LayerID))
		return false;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, transformOut.NumFrames))
		return false;

	// sanity check
	if (transformOut.NumFrames > 10000)
		return false;

	transformOut.Frames.resize(transformOut.NumFrames);
	for (int k = 0; k < transformOut.NumFrames; ++k) {
		VOXTransformFrame frame;
		if (!ReadDictAndIncrement(Buffer, BufferSize, OffsetInOut, frame.FrameAttributes))
			return false;
		frame.Rotation = (uint8_t)frame.FrameAttributes.FindIntValue("_r", 0);
		frame.FrameAttributes.FindInt3Value("_t", frame.Translation.X, frame.Translation.Y, frame.Translation.Z);
		frame.FrameIndex = frame.FrameAttributes.FindIntValue("_f", 0);
		transformOut.Frames[k] = std::move(frame);
	}

	return true;
}


static bool ReadGroupAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, VOXGroup& groupOut)
{
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, groupOut.NodeID))
		return false;
	if (!ReadDictAndIncrement(Buffer, BufferSize, OffsetInOut, groupOut.NodeAttributes))
		return false;
	uint32_t NumChildren;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, NumChildren))
		return false;
	if (NumChildren == 0)
		return true;
	groupOut.Children.resize(NumChildren);
	if (!ReadTypeArrayAndIncrement(Buffer, BufferSize, OffsetInOut, NumChildren, &groupOut.Children[0]))
		return false;
	return true;
}

static bool ReadShapeAndIncrement(const char* Buffer, size_t BufferSize, size_t& OffsetInOut, VOXShape& shapeOut)
{
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, shapeOut.NodeID))
		return false;
	if (!ReadDictAndIncrement(Buffer, BufferSize, OffsetInOut, shapeOut.NodeAttributes))
		return false;
	if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, shapeOut.NumModels))
		return false;
	if (shapeOut.NumModels == 0)
		return true;
	shapeOut.ModelIDs.resize(shapeOut.NumModels);
	shapeOut.ModelAttributes.resize(shapeOut.NumModels);
	for (int k = 0; k < shapeOut.NumModels; ++k)
	{
		int32_t ModelID = -1;
		if (!ReadTypeAndIncrement(Buffer, BufferSize, OffsetInOut, ModelID))
			return false;
		VOXDict ModelAttribtues;
		if (!ReadDictAndIncrement(Buffer, BufferSize, OffsetInOut, ModelAttribtues))
			return false;
		shapeOut.ModelIDs[k] = ModelID;
		shapeOut.ModelAttributes[k] = ModelAttribtues;
	}
	return true;
}



class MagicaVoxReaderInternal
{
public:

	struct VOXScene
	{
		VoxColorPalette UsePalette = VoxColorPalette::StandardPalette();
		std::vector<std::shared_ptr<VoxChunk>> Chunks;
		std::vector<VOXTransform> Transforms;
		std::vector<VOXGroup> Groups;
		std::vector<VOXShape> Shapes;

		const VOXTransform* FindTransformByID(int NodeID) const {
			for (int k = 0; k < Transforms.size(); ++k)
				if (Transforms[k].NodeID == NodeID) return &Transforms[k];
			return nullptr;
		};
		const VOXGroup* FindGroupByID(int NodeID) const {
			for (int k = 0; k < Groups.size(); ++k)
				if (Groups[k].NodeID == NodeID) return &Groups[k];
			return nullptr;
		};
		const VOXShape* FindShapeByID(int NodeID) const {
			for (int k = 0; k < Shapes.size(); ++k)
				if (Shapes[k].NodeID == NodeID) return &Shapes[k];
			return nullptr;
		};
		const VoxChunk* FindChunkByID(int ChunkID) const {
			return (ChunkID >= 0 && ChunkID < Chunks.size()) ? Chunks[ChunkID].get() : nullptr;
		}
		int GetType(int NodeID) const {
			if (FindTransformByID(NodeID) != nullptr) return 0;
			else if (FindGroupByID(NodeID) != nullptr) return 1;
			else if (FindShapeByID(NodeID) != nullptr) return 2;
			return -1;
		}
	};


	void AppendChunkToGrid(
		const VoxChunk& Chunk,
		const VoxColorPalette& Palette,
		MagicaVoxReader::VOXGridObject* AppendToObject,
		GS::Vector3i Translation,
		const GS::MagicaVoxReader::VOXReadOptions& Options) const
	{
		ModelGridCell CurCell = ModelGridCell::SolidCell();
		for (uint32_t k = 0; k < Chunk.NumVoxels; ++k)
		{
			const VoxVoxel& Voxel = Chunk.Voxels[k];
			VoxColor Color = Palette.GetColor(Voxel.ColorIndex);
			GS::Color3b UseColor = (Options.bIgnoreColors) ? GS::Color3b::White() : GS::Color3b(Color.R, Color.G, Color.B);
			CurCell.SetToSolidColor(UseColor);
			Vector3i Index(Voxel.X, Voxel.Y, Voxel.Z);
			Index += Translation;
			AppendToObject->Grid.ReinitializeCell(Index, CurCell);
		}
	}


	void ProcessShape(
		const VOXScene& Scene,
		const VOXShape& Shape,
		FunctionRef<MagicaVoxReader::VOXGridObject* (int)> GetGridObjectFunc,
		std::vector<const VOXTransform*> TransformStack,
		const GS::MagicaVoxReader::VOXReadOptions& Options)
	{
		Vector3i ObjectTranslation = Vector3i::Zero();
		Vector3i AppendTranslation = Vector3i::Zero();
		for (const VOXTransform* Transform : TransformStack) {
			AppendTranslation += Transform->Frames[0].Translation;
		}

		if (Options.bIgnoreTransforms) {
			ObjectTranslation = AppendTranslation;
			AppendTranslation = Vector3i::Zero();
		}

		MagicaVoxReader::VOXGridObject* GridObj = GetGridObjectFunc(Shape.NodeID);
		GridObj->Transform.Translation = ObjectTranslation;
		for (int ModelID : Shape.ModelIDs) {
			const VoxChunk* Chunk = Scene.FindChunkByID(ModelID);
			if (Chunk == nullptr) continue;

			AppendChunkToGrid(*Chunk, Scene.UsePalette, GridObj, AppendTranslation, Options);
		}
	}


	void ProcessTransform(
		const VOXScene& Scene,
		const VOXTransform& Transform,
		FunctionRef<MagicaVoxReader::VOXGridObject*(int)> GetGridObjectFunc,
		std::vector<const VOXTransform*> TransformStack,
		const GS::MagicaVoxReader::VOXReadOptions& Options)
	{
		int ChildType = Scene.GetType(Transform.ChildNodeID);
		if (ChildType == -1)
			return;
		TransformStack.push_back(&Transform);
		if (ChildType == 0) 
			ProcessTransform(Scene, *Scene.FindTransformByID(Transform.ChildNodeID), GetGridObjectFunc, TransformStack, Options);
		else if (ChildType == 1)
			ProcessGroup(Scene, *Scene.FindGroupByID(Transform.ChildNodeID), GetGridObjectFunc, TransformStack, Options);
		else if (ChildType == 2) 
			ProcessShape(Scene, *Scene.FindShapeByID(Transform.ChildNodeID), GetGridObjectFunc, TransformStack, Options);
		TransformStack.pop_back();
	}


	void ProcessGroup(
		const VOXScene& Scene,
		const VOXGroup& Group,
		FunctionRef<MagicaVoxReader::VOXGridObject* (int)> GetGridObjectFunc,
		std::vector<const VOXTransform*> TransformStack,
		const GS::MagicaVoxReader::VOXReadOptions& Options)
	{
		for (int ChildNodeID : Group.Children)
		{
			int ChildType = Scene.GetType(ChildNodeID);
			if (ChildType == -1)
				continue;
			if (ChildType == 0)
				ProcessTransform(Scene, *Scene.FindTransformByID(ChildNodeID), GetGridObjectFunc, TransformStack, Options);
			else if (ChildType == 1)
				ProcessGroup(Scene, *Scene.FindGroupByID(ChildNodeID), GetGridObjectFunc, TransformStack, Options);
			else if (ChildType == 2)
				ProcessShape(Scene, *Scene.FindShapeByID(ChildNodeID), GetGridObjectFunc, TransformStack, Options);
		}
	}


	bool ProcessScene(
		const VOXScene& Scene,
		FunctionRef<MagicaVoxReader::VOXGridObject* ()> RequestNewGridObjectFunc,
		const GS::MagicaVoxReader::VOXReadOptions& Options)
	{
		MagicaVoxReader::VOXGridObject* CombinedGrid = (Options.bCombineAllObjects) ? RequestNewGridObjectFunc() : nullptr;
		auto GetGridObjectFunc = [&](int ID) {
			return (CombinedGrid != nullptr) ? CombinedGrid : RequestNewGridObjectFunc();
		};

		const VOXTransform* RootTransform = Scene.FindTransformByID(0);
		if (RootTransform == nullptr)
			return false;
		std::vector<const VOXTransform*> TransformStack;
		ProcessTransform(Scene, *RootTransform, GetGridObjectFunc, TransformStack, Options);

		return true;
	}


	bool Read(
		const std::string& Path,
		FunctionRef<MagicaVoxReader::VOXGridObject*()> RequestNewGridObjectFunc,
		const GS::MagicaVoxReader::VOXReadOptions& Options)
	{
		std::filesystem::path FilePath(Path);
		if (!std::filesystem::exists(FilePath))
			return false;

		size_t FileSize = std::filesystem::file_size(FilePath);

		//FILE* FilePtr = std::fopen_s(Path.c_str(), "r");
		FILE* FilePtr = nullptr;
		errno_t error = fopen_s(&FilePtr, Path.c_str(), "rb");
		if (!FilePtr)
			return false;

		std::vector<char> Buffer;
		Buffer.resize(FileSize);

		size_t ReadBytes = fread((void*)&Buffer[0], 1, FileSize, FilePtr);
		fclose(FilePtr);

		if (ReadBytes != FileSize)
		{
			return false;
		}
		const char* CurBufferPtr = &Buffer[0];
		size_t Offset = 0;

		BinaryHeader<4> VoxHeader;
		size_t HeaderBytes = VoxHeader.Read(CurBufferPtr, ReadBytes, Offset);
		if (HeaderBytes == 0 || VoxHeader.BeginsWith("VOX ", 4) == false)
			return false;
		Offset += HeaderBytes;

		uint32_t VersionNumber;
		if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, VersionNumber))
			return false;

		VoxChunkHeader MainHeader;
		if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, MainHeader))
			return false;

		if (MainHeader.ChunkBytes != 0) 
			return false;		// main chunk is always just a list of child chunks?

		VOXScene Scene;

		while (Offset + sizeof(VoxChunkHeader) < FileSize)
		{
			VoxChunkHeader CurHeader;
			if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, CurHeader))
				return false;

			if (CurHeader.IsChunkID("PACK"))
				return false;		// not supported for now?

			bool bConsumedHeaderData = false;
			if (CurHeader.IsChunkID("SIZE"))
			{
				VoxChunkHeader SizeHeader = CurHeader;
				if (SizeHeader.ChunkBytes != 12 || SizeHeader.ChildrenChunksBytes != 0)
					return false;		// not going to work...

				std::shared_ptr<VoxChunk> NewChunk = std::make_shared<VoxChunk>();

				if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, NewChunk->Size))
					return false;
				// todo sanity check size? does it matter?

				VoxChunkHeader XYZIHeader;
				if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, XYZIHeader))
					return false;
				if (XYZIHeader.ChildrenChunksBytes != 0) return false;

				if (!ReadTypeAndIncrement(CurBufferPtr, FileSize, Offset, NewChunk->NumVoxels))
					return false;
				if (NewChunk->NumVoxels == 0)
					return false;

				NewChunk->Voxels.resize(NewChunk->NumVoxels);
				if (!ReadTypeArrayAndIncrement(CurBufferPtr, FileSize, Offset, NewChunk->NumVoxels, &NewChunk->Voxels[0]))
					return false;

				Scene.Chunks.push_back(NewChunk);

				bConsumedHeaderData = true;
			}
			else if (CurHeader.IsChunkID("RGBA"))
			{	
				uint32_t PaletteBuffer[256];
				if (!ReadTypeArrayAndIncrement(CurBufferPtr, FileSize, Offset, 256, PaletteBuffer))
					return false;
				for (int i = 0; i <= 254; i++) 
					Scene.UsePalette.Palette[i+1] = PaletteBuffer[i];
			}
			else if (CurHeader.IsChunkID("nTRN"))
			{
				VOXTransform TransformRecord;
				if (!ReadTransformAndIncrement(CurBufferPtr, FileSize, Offset, TransformRecord))
					return false;
				Scene.Transforms.push_back(TransformRecord);
			}
			else if (CurHeader.IsChunkID("nGRP"))
			{
				VOXGroup GroupRecord;
				if (!ReadGroupAndIncrement(CurBufferPtr, FileSize, Offset, GroupRecord))
					return false;
				Scene.Groups.push_back(GroupRecord);
			}
			else if (CurHeader.IsChunkID("nSHP"))
			{
				VOXShape ShapeRecord;
				if (!ReadShapeAndIncrement(CurBufferPtr, FileSize, Offset, ShapeRecord))
					return false;
				Scene.Shapes.push_back(ShapeRecord);
			}
			else
			{
				//UE_LOG(LogTemp, Warning, TEXT("VOX: Unknown chunk %c%c%c%c"), CurHeader.ChunkID[0], CurHeader.ChunkID[1], CurHeader.ChunkID[2], CurHeader.ChunkID[3]);
				Offset += CurHeader.ChunkBytes + CurHeader.ChildrenChunksBytes;
			}
		}

		ProcessScene(Scene, RequestNewGridObjectFunc, Options);
		return true;
	}
};





bool MagicaVoxReader::Read(
	const std::string& Path,
	FunctionRef<VOXGridObject*()> RequestNewGridObjectFunc,
	const VOXReadOptions& Options)
{
	MagicaVoxReaderInternal Reader;
	return Reader.Read(Path, RequestNewGridObjectFunc, Options);
}
