/**
 * Copyright (c) 2014-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <cstdint>
#include <cassert>
#include <iostream>
#include <fstream>

#include <stb_image.h>
#include <stb_image_write.h>

#include "FBX2glTF.h"
#include "mathfu.h"
#include "utils/String_Utils.h"
#include "utils/Image_Utils.h"
#include "utils/File_Utils.h"
#include "RawModel.h"
#include "Raw2Cross.h"
#include "PVRTGeometry.h"

#define ALIGN_BYTE(a, b) ((((a) + (b) - 1) / (b)) * (b))
#define ALIGN_4BYTE(a)   ALIGN_BYTE(a, 4)

typedef struct MeshHeader
{
	unsigned int indexBufferSize;
	unsigned int indexBufferOffset;
	
	unsigned int vertexBufferSize;
	unsigned int vertexBufferOffset;
	
} MeshHeader;

static void WriteAlign(FILE *pFile)
{
	char c = 0xcc;

	unsigned int size = ftell(pFile);
	unsigned int alignSize = ALIGN_4BYTE(size);

	for (int index = size; index < alignSize; index++) {
		fwrite(&c, sizeof(c), 1, pFile);
	}
}

static void splitfilename(const char *name, char *fname, char *ext)
{
	const char *p = NULL;
	const char *c = NULL;
	const char *base = name;

	for (p = base; *p; p++) {
		if (*p == '/' || *p == '\\') {
			do {
				p++;
			} while (*p == '/' || *p == '\\');

			base = p;
		}
	}

	size_t len = strlen(base);
	for (p = base + len; p != base && *p != '.'; p--);
	if (p == base && *p != '.') p = base + len;

	if (fname) {
		for (c = base; c < p; c++) {
			fname[c - base] = *c;
		}

		fname[c - base] = 0;
	}

	if (ext) {
		for (c = p; c < base + len; c++) {
			ext[c - p] = *c;
		}

		ext[c - p] = 0;
	}
}

static unsigned int GetVertexSize(unsigned int format)
{
	unsigned int size = 0;

	if (format & RAW_VERTEX_ATTRIBUTE_POSITION) {
		size += sizeof(float) * 3;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_NORMAL) {
		size += sizeof(float) * 3;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_BINORMAL) {
		size += sizeof(float) * 3;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_COLOR) {
		size += sizeof(float) * 3;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_UV0) {
		size += sizeof(float) * 2;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_UV1) {
		size += sizeof(float) * 2;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_JOINT_INDICES) {
		size += sizeof(float) * 4;
	}
	if (format & RAW_VERTEX_ATTRIBUTE_JOINT_WEIGHTS) {
		size += sizeof(float) * 4;
	}

	return size;
}

static bool ExportMesh(const char *szFileName, const RawModel &model, const RawModel &raw, bool bWorldSpace)
{
	MeshHeader header;
	{
		const unsigned int baseOffset = 44;

		header.indexBufferSize = model.GetTriangleCount() * 3 * sizeof(unsigned int);
		header.indexBufferOffset = ALIGN_4BYTE(baseOffset);

		header.vertexBufferSize = model.GetVertexCount() * GetVertexSize(model.GetVertexAttributes());
		header.vertexBufferOffset = ALIGN_4BYTE(baseOffset) + ALIGN_4BYTE(header.indexBufferSize);
	}

	std::vector<RawVertex> vertices;
	std::vector<unsigned int> indices;
	{
		for (int index = 0; index < model.GetVertexCount(); index++) {
			vertices.push_back(model.GetVertex(index));
		}

		for (int index = 0; index < model.GetTriangleCount(); index++) {
			const RawTriangle &triangle = model.GetTriangle(index);
			indices.push_back(triangle.verts[0]);
			indices.push_back(triangle.verts[1]);
			indices.push_back(triangle.verts[2]);
		}

		PVRTGeometrySort(vertices.data(), indices.data(), sizeof(RawVertex), vertices.size(), indices.size() / 3, vertices.size(), indices.size() / 3, PVRTGEOMETRY_SORT_VERTEXCACHE);
	}

	if (bWorldSpace)
	{
		Mat4f matrix(
			1.0f, 0.0f, 0.0f, 0.0f, 
			0.0f, 1.0f, 0.0f, 0.0f, 
			0.0f, 0.0f, 1.0f, 0.0f, 
			0.0f, 0.0f, 0.0f, 1.0f);

		long indexNode = raw.GetNodeById(model.GetSurface(0).skeletonRootId);

		while (indexNode != -1) {
			RawNode node = raw.GetNode(indexNode);
			{
				Mat4f scale = Mat4f::FromScaleVector(node.scale);
				Mat4f rotate = Mat4f::FromRotationMatrix(node.rotation.ToMatrix());
				Mat4f translate = Mat4f::FromTranslationVector(node.translation);
				matrix = matrix * translate * rotate * scale;
			}
			indexNode = raw.GetNodeById(node.parentId);
		}

		for (int index = 0; index < vertices.size(); index++) {
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_POSITION) {
				vertices[index].position = matrix * vertices[index].position;
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_NORMAL) {
				vertices[index].normal = matrix * vertices[index].normal;
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_BINORMAL) {
				vertices[index].binormal = matrix * vertices[index].binormal;
			}
		}
	}

	FILE *pFile = fopen(szFileName, "wb");
	if (pFile == NULL) return false;
	{
		fwrite(&header, sizeof(header), 1, pFile);

		unsigned int format = model.GetVertexAttributes();
		fwrite(&format, sizeof(format), 1, pFile);

		Bounds<float, 3> bounds = model.GetSurface(0).bounds;
		fwrite(&bounds.min.x, sizeof(bounds.min.x), 1, pFile);
		fwrite(&bounds.min.y, sizeof(bounds.min.y), 1, pFile);
		fwrite(&bounds.min.z, sizeof(bounds.min.z), 1, pFile);
		fwrite(&bounds.max.x, sizeof(bounds.max.x), 1, pFile);
		fwrite(&bounds.max.y, sizeof(bounds.max.y), 1, pFile);
		fwrite(&bounds.max.z, sizeof(bounds.max.z), 1, pFile);

		WriteAlign(pFile);

		for (int index = 0; index < indices.size(); index++) {
			fwrite(&indices[index], sizeof(indices[index]), 1, pFile);
		}

		WriteAlign(pFile);

		for (int index = 0; index < vertices.size(); index++) {
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_POSITION) {
				fwrite(&vertices[index].position.x, sizeof(vertices[index].position.x), 1, pFile);
				fwrite(&vertices[index].position.y, sizeof(vertices[index].position.y), 1, pFile);
				fwrite(&vertices[index].position.z, sizeof(vertices[index].position.z), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_NORMAL) {
				fwrite(&vertices[index].normal.x, sizeof(vertices[index].normal.x), 1, pFile);
				fwrite(&vertices[index].normal.y, sizeof(vertices[index].normal.y), 1, pFile);
				fwrite(&vertices[index].normal.z, sizeof(vertices[index].normal.z), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_BINORMAL) {
				fwrite(&vertices[index].binormal.x, sizeof(vertices[index].binormal.x), 1, pFile);
				fwrite(&vertices[index].binormal.y, sizeof(vertices[index].binormal.y), 1, pFile);
				fwrite(&vertices[index].binormal.z, sizeof(vertices[index].binormal.z), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_COLOR) {
				fwrite(&vertices[index].color.x, sizeof(vertices[index].color.x), 1, pFile);
				fwrite(&vertices[index].color.y, sizeof(vertices[index].color.y), 1, pFile);
				fwrite(&vertices[index].color.z, sizeof(vertices[index].color.z), 1, pFile);
//				fwrite(&vertices[index].color.w, sizeof(vertices[index].color.w), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_UV0) {
				fwrite(&vertices[index].uv0.x, sizeof(vertices[index].uv0.x), 1, pFile);
				fwrite(&vertices[index].uv0.y, sizeof(vertices[index].uv0.y), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_UV1) {
				fwrite(&vertices[index].uv1.x, sizeof(vertices[index].uv1.x), 1, pFile);
				fwrite(&vertices[index].uv1.y, sizeof(vertices[index].uv1.y), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_JOINT_INDICES) {
				fwrite(&vertices[index].jointIndices.x, sizeof(vertices[index].jointIndices.x), 1, pFile);
				fwrite(&vertices[index].jointIndices.y, sizeof(vertices[index].jointIndices.y), 1, pFile);
				fwrite(&vertices[index].jointIndices.z, sizeof(vertices[index].jointIndices.z), 1, pFile);
				fwrite(&vertices[index].jointIndices.w, sizeof(vertices[index].jointIndices.w), 1, pFile);
			}
			if (model.GetVertexAttributes() & RAW_VERTEX_ATTRIBUTE_JOINT_WEIGHTS) {
				fwrite(&vertices[index].jointWeights.x, sizeof(vertices[index].jointWeights.x), 1, pFile);
				fwrite(&vertices[index].jointWeights.y, sizeof(vertices[index].jointWeights.y), 1, pFile);
				fwrite(&vertices[index].jointWeights.z, sizeof(vertices[index].jointWeights.z), 1, pFile);
				fwrite(&vertices[index].jointWeights.w, sizeof(vertices[index].jointWeights.w), 1, pFile);
			}
		}
	}
	fclose(pFile);

	return true;
}

bool ExportMeshs(const char *szPathName, const RawModel &raw, bool bWorldSpace)
{
	std::vector<RawModel> materialModels;
	raw.CreateMaterialModels(materialModels, false, -1, true);

	for (int index = 0; index < materialModels.size(); index++) {
		char szFileName[_MAX_PATH];
		sprintf(szFileName, "%s/%s.mesh", szPathName, materialModels[index].GetSurface(0).name.c_str());
		ExportMesh(szFileName, materialModels[index], raw, bWorldSpace);
	}

	return true;
}

static bool ExportMaterial(const char *szFileName, const RawMaterial &material, const RawModel &raw)
{
	FILE *pFile = fopen(szFileName, "wb");
	if (pFile == NULL) return false;
	{
		fprintf(pFile, "<Material>\n");
		{
			switch (material.type) {
			case RAW_MATERIAL_TYPE_OPAQUE:
				fprintf(pFile, "\t<Pass name=\"Opaque\" graphics=\"DiffuseForwardOpaquePresent.graphics\">\n");
				break;
			case RAW_MATERIAL_TYPE_TRANSPARENT:
				fprintf(pFile, "\t<Pass name=\"Transparent\" graphics=\"DiffuseForwardTransparentPresent.graphics\">\n");
				break;
			case RAW_MATERIAL_TYPE_SKINNED_OPAQUE:
				fprintf(pFile, "\t<Pass name=\"SkinOpaque\" graphics=\"DiffuseForwardSkinOpaquePresent.graphics\">\n");
				break;
			case RAW_MATERIAL_TYPE_SKINNED_TRANSPARENT:
				fprintf(pFile, "\t<Pass name=\"SkinTransparent\" graphics=\"DiffuseForwardSkinTransparentPresent.graphics\">\n");
				break;
			}
			{
				for (int index = 0; index < RAW_TEXTURE_USAGE_MAX; index++) {
					if (material.textures[index] != -1) {
						char szExt[_MAX_PATH];
						char szFName[_MAX_PATH];
						char szFileName[_MAX_PATH];
						splitfilename(raw.GetTexture(material.textures[index]).fileName.c_str(), szFName, szExt);
						sprintf(szFileName, "%s%s", szFName, szExt);
						fprintf(pFile, "\t\t<Texture2D name=\"%s\" value=\"%s\" />\n", Describe((RawTextureUsage)index).c_str(), szFileName);
					}
				}
			}
			fprintf(pFile, "\t</Pass>\n");
		}
		fprintf(pFile, "</Material>\n");
	}
	fclose(pFile);
	return true;

	switch (material.type) {
	case RAW_MATERIAL_TYPE_OPAQUE:
		break;
	case RAW_MATERIAL_TYPE_TRANSPARENT:
		break;
	case RAW_MATERIAL_TYPE_SKINNED_OPAQUE:
		break;
	case RAW_MATERIAL_TYPE_SKINNED_TRANSPARENT:
		break;
	}

	if (material.info->shadingModel == RAW_SHADING_MODEL_PBR_MET_ROUGH) {
		const RawMetRoughMatProps *props = (RawMetRoughMatProps *)material.info.get();
		int a = 0;
	}
	else {
		const RawTraditionalMatProps *props = ((RawTraditionalMatProps *)material.info.get());

		if (material.info->shadingModel == RAW_SHADING_MODEL_BLINN ||
			material.info->shadingModel == RAW_SHADING_MODEL_PHONG) {

		}
	}

	return true;
}

bool ExportMaterials(const char *szPathName, const RawModel &raw)
{
	for (int index = 0; index < raw.GetMaterialCount(); index++) {
		char szFileName[_MAX_PATH];
		sprintf(szFileName, "%s/%s.material", szPathName, raw.GetMaterial(index).name.c_str());
		ExportMaterial(szFileName, raw.GetMaterial(index), raw);
	}
	return true;
}
