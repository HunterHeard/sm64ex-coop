#include "dynos.cpp.h"

  /////////////
 // Parsing //
/////////////

static TexData* ParseTexListSymbol(GfxData* aGfxData, DataNode<TexData*>* aNode, String& aToken) {
    // Textures
    for (auto& _Node : aGfxData->mTextures) {
        if (aToken == _Node->mName) {
            return DynOS_Tex_Parse(aGfxData, _Node)->mData;
        }
    }

    // Unknown
    PrintError("  ERROR: Unknown texlist arg: %s", aToken.begin());
    return NULL;
}

DataNode<TexData*>* DynOS_TexList_Parse(GfxData* aGfxData, DataNode<TexData*>* aNode) {
    if (aNode->mData) return aNode;

    // TexList data
    aNode->mSize = (u32) (aNode->mTokens.Count());

    aNode->mData = New<TexData*>(aNode->mSize);
    for (u32 i = 0; i != aNode->mSize; ++i) {
        aNode->mData[i] = ParseTexListSymbol(aGfxData, aNode, aNode->mTokens[i]);
        aGfxData->mPointerList.Add(&aNode->mData[i]);
    }
    aNode->mLoadIndex = aGfxData->mLoadIndex++;
    return aNode;
}

  /////////////
 // Writing //
/////////////

void DynOS_TexList_Write(FILE* aFile, GfxData* aGfxData, DataNode<TexData*> *aNode) {
    if (!aNode->mData) return;

    // Name
    WriteBytes<u8>(aFile, DATA_TYPE_TEXTURE_LIST);
    aNode->mName.Write(aFile);

    // Data
    WriteBytes<u32>(aFile, aNode->mSize);
    for (u32 i = 0; i != aNode->mSize; ++i) {
        // find node
        bool found = false;
        for (auto& _Node : aGfxData->mTextures) {
            if (_Node->mData == aNode->mData[i]) {
                DynOS_Pointer_Write(aFile, (const void *) (_Node), aGfxData);
                found = true;
                break;
            }
        }
        if (!found) {
            PrintError("Could not write texture in texlist");
        }
    }
}

  /////////////
 // Reading //
/////////////

DataNode<TexData*>* DynOS_TexList_Load(FILE *aFile, GfxData *aGfxData) {
    DataNode<TexData*> *_Node = New<DataNode<TexData*>>();

    // Name
    _Node->mName.Read(aFile);

    // Data
    _Node->mSize = ReadBytes<u32>(aFile);
    _Node->mData = New<TexData*>(_Node->mSize);
    for (u32 i = 0; i != _Node->mSize; ++i) {
        u32 _Value = ReadBytes<u32>(aFile);
        void *_Ptr = DynOS_Pointer_Load(aFile, aGfxData, _Value);
        if (_Ptr == NULL) {
            PrintError("Could not read texture in texlist");
        } else {
            _Node->mData[i] = ((DataNode<TexData>*)_Ptr)->mData;
        }
    }

    // Add it
    if (aGfxData != NULL) {
        aGfxData->mTextureLists.Add(_Node);
    }

    return _Node;
}
