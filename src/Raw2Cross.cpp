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
#include "tinyxml.h"
#include "tinystr.h"

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

static std::string GetModelFileName(const char *szPathName, const RawSurface &rawSurface)
{
	char szFileName[_MAX_PATH];
	sprintf(szFileName, "%s/%s.mesh", szPathName, rawSurface.name.c_str());
	return std::string(szFileName);
}

static std::string GetMaterialFileName(const char *szPathName, const RawMaterial &rawMaterial)
{
	char szFileName[_MAX_PATH];
	sprintf(szFileName, "%s/%s.material", szPathName, rawMaterial.name.c_str());
	return std::string(szFileName);
}

static bool ExportMesh(const char *szFileName, const RawModel &model, const RawModel &raw, bool bWorldSpace)
{
	unsigned int format = model.GetVertexAttributes();

	MeshHeader header;
	{
		const unsigned int baseOffset = 44;

		header.indexBufferSize = model.GetTriangleCount() * 3 * sizeof(unsigned int);
		header.indexBufferOffset = ALIGN_4BYTE(baseOffset);

		header.vertexBufferSize = model.GetVertexCount() * GetVertexSize(format);
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
			if (format & RAW_VERTEX_ATTRIBUTE_POSITION) {
				vertices[index].position = matrix * vertices[index].position;
			}
			if (format & RAW_VERTEX_ATTRIBUTE_NORMAL) {
				vertices[index].normal = matrix * vertices[index].normal;
			}
			if (format & RAW_VERTEX_ATTRIBUTE_BINORMAL) {
				vertices[index].binormal = matrix * vertices[index].binormal;
			}
		}
	}

	Bounds<float, 3> bounds;
	{
		bounds.min.x = bounds.min.y = bounds.min.z =  FLT_MAX;
		bounds.max.x = bounds.max.y = bounds.max.z = -FLT_MAX;

		for (int index = 0; index < vertices.size(); index++) {
			if (bounds.min.x > vertices[index].position.x) bounds.min.x = vertices[index].position.x;
			if (bounds.min.y > vertices[index].position.y) bounds.min.y = vertices[index].position.y;
			if (bounds.min.z > vertices[index].position.z) bounds.min.z = vertices[index].position.z;

			if (bounds.max.x < vertices[index].position.x) bounds.max.x = vertices[index].position.x;
			if (bounds.max.y < vertices[index].position.y) bounds.max.y = vertices[index].position.y;
			if (bounds.max.z < vertices[index].position.z) bounds.max.z = vertices[index].position.z;
		}
	}

	FILE *pFile = fopen(szFileName, "wb");
	if (pFile == NULL) return false;
	{
		fwrite(&header, sizeof(header), 1, pFile);

		fwrite(&format, sizeof(format), 1, pFile);

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
			if (format & RAW_VERTEX_ATTRIBUTE_POSITION) {
				fwrite(&vertices[index].position.x, sizeof(vertices[index].position.x), 1, pFile);
				fwrite(&vertices[index].position.y, sizeof(vertices[index].position.y), 1, pFile);
				fwrite(&vertices[index].position.z, sizeof(vertices[index].position.z), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_NORMAL) {
				fwrite(&vertices[index].normal.x, sizeof(vertices[index].normal.x), 1, pFile);
				fwrite(&vertices[index].normal.y, sizeof(vertices[index].normal.y), 1, pFile);
				fwrite(&vertices[index].normal.z, sizeof(vertices[index].normal.z), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_BINORMAL) {
				fwrite(&vertices[index].binormal.x, sizeof(vertices[index].binormal.x), 1, pFile);
				fwrite(&vertices[index].binormal.y, sizeof(vertices[index].binormal.y), 1, pFile);
				fwrite(&vertices[index].binormal.z, sizeof(vertices[index].binormal.z), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_COLOR) {
				fwrite(&vertices[index].color.x, sizeof(vertices[index].color.x), 1, pFile);
				fwrite(&vertices[index].color.y, sizeof(vertices[index].color.y), 1, pFile);
				fwrite(&vertices[index].color.z, sizeof(vertices[index].color.z), 1, pFile);
//				fwrite(&vertices[index].color.w, sizeof(vertices[index].color.w), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_UV0) {
				fwrite(&vertices[index].uv0.x, sizeof(vertices[index].uv0.x), 1, pFile);
				fwrite(&vertices[index].uv0.y, sizeof(vertices[index].uv0.y), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_UV1) {
				fwrite(&vertices[index].uv1.x, sizeof(vertices[index].uv1.x), 1, pFile);
				fwrite(&vertices[index].uv1.y, sizeof(vertices[index].uv1.y), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_JOINT_INDICES) {
				fwrite(&vertices[index].jointIndices.x, sizeof(vertices[index].jointIndices.x), 1, pFile);
				fwrite(&vertices[index].jointIndices.y, sizeof(vertices[index].jointIndices.y), 1, pFile);
				fwrite(&vertices[index].jointIndices.z, sizeof(vertices[index].jointIndices.z), 1, pFile);
				fwrite(&vertices[index].jointIndices.w, sizeof(vertices[index].jointIndices.w), 1, pFile);
			}
			if (format & RAW_VERTEX_ATTRIBUTE_JOINT_WEIGHTS) {
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

bool ExportMeshs(const char *szPathName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels, bool bWorldSpace)
{
	for (int index = 0; index < rawMaterialModels.size(); index++) {
		std::string fileName = GetModelFileName(szPathName, rawMaterialModels[index].GetSurface(0));
		ExportMesh(fileName.c_str(), rawMaterialModels[index], rawModel, bWorldSpace);
	}

	return true;
}

static bool ExportMaterial(const char *szFileName, const RawMaterial &material, const RawModel &raw)
{
	TiXmlDocument doc;
	TiXmlElement *pMaterialNode = new TiXmlElement("Material");
	{
		for (int index = 0; index < RAW_TEXTURE_USAGE_MAX; index++) {
			if (material.textures[index] == -1) {
				continue;
			}

			TiXmlElement *pTextureNode = new TiXmlElement("Texture2D");
			{
				char szExt[_MAX_PATH];
				char szFName[_MAX_PATH];
				char szFileName[_MAX_PATH];
				splitfilename(raw.GetTexture(material.textures[index]).fileName.c_str(), szFName, szExt);
				sprintf(szFileName, "%s%s", szFName, szExt);

				switch (material.textures[index]) {
				case RAW_TEXTURE_USAGE_AMBIENT:   pTextureNode->SetAttributeString("name", "%s", "texAmbient");    break;
				case RAW_TEXTURE_USAGE_DIFFUSE:   pTextureNode->SetAttributeString("name", "%s", "texDiffuse");    break;
				case RAW_TEXTURE_USAGE_NORMAL:    pTextureNode->SetAttributeString("name", "%s", "texNormal");     break;
				case RAW_TEXTURE_USAGE_SPECULAR:  pTextureNode->SetAttributeString("name", "%s", "texSpecular");   break;
				case RAW_TEXTURE_USAGE_SHININESS: pTextureNode->SetAttributeString("name", "%s", "texShininess");  break;
				case RAW_TEXTURE_USAGE_EMISSIVE:  pTextureNode->SetAttributeString("name", "%s", "texEmissive");   break;
				case RAW_TEXTURE_USAGE_REFLECTION:pTextureNode->SetAttributeString("name", "%s", "texReflection"); break;
				case RAW_TEXTURE_USAGE_ALBEDO:    pTextureNode->SetAttributeString("name", "%s", "texAlbedo");     break;
				case RAW_TEXTURE_USAGE_OCCLUSION: pTextureNode->SetAttributeString("name", "%s", "texOcclusion");  break;
				case RAW_TEXTURE_USAGE_ROUGHNESS: pTextureNode->SetAttributeString("name", "%s", "texRoughness");  break;
				case RAW_TEXTURE_USAGE_METALLIC:  pTextureNode->SetAttributeString("name", "%s", "texMetallic");   break;
				}
				pTextureNode->SetAttributeString("file_name", "%s", szFileName);
				pTextureNode->SetAttributeString("min_filter", "%s", "GL_LINEAR_MIPMAP_NEAREST");
				pTextureNode->SetAttributeString("mag_filter", "%s", "GL_LINEAR");
				pTextureNode->SetAttributeString("address_mode", "%s", "GL_CLAMP_TO_EDGE");
			}
			pMaterialNode->LinkEndChild(pTextureNode);
		}
	}
	doc.LinkEndChild(pMaterialNode);
	return doc.SaveFile(szFileName);
}

bool ExportMaterials(const char *szPathName, const RawModel &rawModel)
{
	for (int index = 0; index < rawModel.GetMaterialCount(); index++) {
		std::string fileName = GetMaterialFileName(szPathName, rawModel.GetMaterial(index));
		ExportMaterial(fileName.c_str(), rawModel.GetMaterial(index), rawModel);
	}
	return true;
}

static void ExportNodeMesh(TiXmlElement *pParentNode, const RawNode &node, std::unordered_map<long, std::string> &surfaceMeshs, std::unordered_map<long, std::string> &surfaceMaterials)
{
	if (node.surfaceId != -1) {
		TiXmlElement *pMeshNode = new TiXmlElement("Mesh");
		{
			pMeshNode->SetAttributeString("mesh", "%s", surfaceMeshs[node.surfaceId].c_str());
			pMeshNode->SetAttributeString("material", "%s", surfaceMaterials[node.surfaceId].c_str());
		}
		pParentNode->LinkEndChild(pMeshNode);
	}
}

static void ExportNode(TiXmlElement *pParentNode, const long id, const RawModel &rawModel, std::unordered_map<long, std::string> &surfaceMeshs, std::unordered_map<long, std::string> &surfaceMaterials)
{
	TiXmlElement *pCurrentNode = new TiXmlElement("Node");
	{
		const RawNode &node = rawModel.GetNode(rawModel.GetNodeById(id));
		pCurrentNode->SetAttributeString("translation", "%f %f %f", node.translation.x, node.translation.y, node.translation.z);
		pCurrentNode->SetAttributeString("rotation", "%f %f %f %f", node.rotation[1], node.rotation[2], node.rotation[3], node.rotation[0]);
		pCurrentNode->SetAttributeString("scale", "%f %f %f", node.scale.x, node.scale.y, node.scale.z);

		ExportNodeMesh(pCurrentNode, node, surfaceMeshs, surfaceMaterials);

		for (int indexChild = 0; indexChild < node.childIds.size(); indexChild++) {
			ExportNode(pCurrentNode, node.childIds[indexChild], rawModel, surfaceMeshs, surfaceMaterials);
		}
	}
	pParentNode->LinkEndChild(pCurrentNode);
}

bool ExportScene(const char *szPathName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels)
{
	char szFileName[_MAX_PATH];
	sprintf(szFileName, "%s/Scene.xml", szPathName);

	std::unordered_map<long, std::string> surfaceMeshs;
	std::unordered_map<long, std::string> surfaceMaterials;
	for (int index = 0; index < rawMaterialModels.size(); index++) {
		long id = rawMaterialModels[index].GetSurface(0).id;
		surfaceMeshs[id] = GetModelFileName(".", rawMaterialModels[index].GetSurface(0));
		surfaceMaterials[id] = GetMaterialFileName(".", rawMaterialModels[index].GetMaterial(0));
	}

	TiXmlDocument doc;
	TiXmlElement *pSceneNode = new TiXmlElement("Scene");
	{
		ExportNode(pSceneNode, rawModel.GetRootNode(), rawModel, surfaceMeshs, surfaceMaterials);
	}
	doc.LinkEndChild(pSceneNode);
	return doc.SaveFile(szFileName);
}
