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

void splitfilename(const char *name, char *fname, char *ext)
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

static std::string GetMaterialFileName(const char *szPathName, const RawMaterial &rawMaterial)
{
	char szFileName[_MAX_PATH];
	sprintf(szFileName, "%s%s.material", szPathName, rawMaterial.name.c_str());
	return std::string(szFileName);
}

static bool ExportMeshHeader(FILE *pFile, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels)
{
	typedef struct SubMeshHeader
	{
		float minx =  FLT_MAX;
		float miny =  FLT_MAX;
		float minz =  FLT_MAX;
		float maxx = -FLT_MAX;
		float maxy = -FLT_MAX;
		float maxz = -FLT_MAX;

		unsigned int baseVertex = 0;
		unsigned int firstIndex = 0;
		unsigned int indexCount = 0;

	} SubMeshHeader;

	typedef struct MeshHeader
	{
		unsigned int format = 0;
		unsigned int numSubMeshs = 0;

		unsigned int indexBufferSize = 0;
		unsigned int indexBufferOffset = 0;

		unsigned int vertexBufferSize = 0;
		unsigned int vertexBufferOffset = 0;

		std::vector<SubMeshHeader> subMeshHeaders;

	} MeshHeader;


	unsigned int numIndex = 0;
	unsigned int numVertex = 0;

	MeshHeader meshHeader;
	meshHeader.format = rawModel.GetVertexAttributes();
	meshHeader.numSubMeshs = rawMaterialModels.size();
	meshHeader.subMeshHeaders.resize(rawMaterialModels.size());

	for (int indexMesh = 0; indexMesh < rawMaterialModels.size(); indexMesh++) {
		for (int indexVertex = 0; indexVertex < rawMaterialModels[indexMesh].GetVertexCount(); indexVertex++) {
			const RawVertex &vertex = rawMaterialModels[indexMesh].GetVertex(indexVertex);

			if (meshHeader.subMeshHeaders[indexMesh].minx > vertex.position.x) meshHeader.subMeshHeaders[indexMesh].minx = vertex.position.x;
			if (meshHeader.subMeshHeaders[indexMesh].miny > vertex.position.y) meshHeader.subMeshHeaders[indexMesh].miny = vertex.position.y;
			if (meshHeader.subMeshHeaders[indexMesh].minz > vertex.position.z) meshHeader.subMeshHeaders[indexMesh].minz = vertex.position.z;

			if (meshHeader.subMeshHeaders[indexMesh].maxx < vertex.position.x) meshHeader.subMeshHeaders[indexMesh].maxx = vertex.position.x;
			if (meshHeader.subMeshHeaders[indexMesh].maxy < vertex.position.y) meshHeader.subMeshHeaders[indexMesh].maxy = vertex.position.y;
			if (meshHeader.subMeshHeaders[indexMesh].maxz < vertex.position.z) meshHeader.subMeshHeaders[indexMesh].maxz = vertex.position.z;
		}

		meshHeader.subMeshHeaders[indexMesh].baseVertex = 0; // numVertex;
		meshHeader.subMeshHeaders[indexMesh].firstIndex = numIndex;
		meshHeader.subMeshHeaders[indexMesh].indexCount = rawMaterialModels[indexMesh].GetTriangleCount() * 3;

		meshHeader.indexBufferSize += rawMaterialModels[indexMesh].GetTriangleCount() * 3 * sizeof(unsigned int);
		meshHeader.vertexBufferSize += rawMaterialModels[indexMesh].GetVertexCount() * GetVertexSize(meshHeader.format);

		numIndex += rawMaterialModels[indexMesh].GetTriangleCount() * 3;
		numVertex += rawMaterialModels[indexMesh].GetVertexCount();
	}
	
	const unsigned int baseOffset = offsetof(MeshHeader, subMeshHeaders) + sizeof(SubMeshHeader) * meshHeader.subMeshHeaders.size();
	meshHeader.indexBufferOffset = baseOffset;
	meshHeader.vertexBufferOffset = baseOffset + meshHeader.indexBufferSize;

	fwrite(&meshHeader.format, sizeof(meshHeader.format), 1, pFile);
	fwrite(&meshHeader.numSubMeshs, sizeof(meshHeader.numSubMeshs), 1, pFile);
	fwrite(&meshHeader.indexBufferSize, sizeof(meshHeader.indexBufferSize), 1, pFile);
	fwrite(&meshHeader.indexBufferOffset, sizeof(meshHeader.indexBufferOffset), 1, pFile);
	fwrite(&meshHeader.vertexBufferSize, sizeof(meshHeader.vertexBufferSize), 1, pFile);
	fwrite(&meshHeader.vertexBufferOffset, sizeof(meshHeader.vertexBufferOffset), 1, pFile);
	fwrite(meshHeader.subMeshHeaders.data(), sizeof(SubMeshHeader), meshHeader.subMeshHeaders.size(), pFile);

	return true;
}

static bool ExportMeshData(FILE *pFile, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels)
{
	unsigned long format = rawModel.GetVertexAttributes();

	std::vector<RawVertex> vertices;
	std::vector<unsigned int> indices;

	for (int indexMesh = 0; indexMesh < rawMaterialModels.size(); indexMesh++) {
		int baseIndex = indices.size();
		int baseVertex = vertices.size();

		for (int index = 0; index < rawMaterialModels[indexMesh].GetVertexCount(); index++) {
			vertices.push_back(rawMaterialModels[indexMesh].GetVertex(index));
		}

		for (int index = 0; index < rawMaterialModels[indexMesh].GetTriangleCount(); index++) {
			const RawTriangle &triangle = rawMaterialModels[indexMesh].GetTriangle(index);
			indices.push_back(triangle.verts[0]);
			indices.push_back(triangle.verts[1]);
			indices.push_back(triangle.verts[2]);
		}

		PVRTGeometrySort(
			&vertices[baseVertex],
			&indices[baseIndex],
			sizeof(RawVertex),
			rawMaterialModels[indexMesh].GetVertexCount(),
			rawMaterialModels[indexMesh].GetTriangleCount(),
			rawMaterialModels[indexMesh].GetVertexCount(),
			rawMaterialModels[indexMesh].GetTriangleCount(),
			PVRTGEOMETRY_SORT_VERTEXCACHE);

		for (int index = 0; index < rawMaterialModels[indexMesh].GetTriangleCount(); index++) {
			indices[baseIndex + 3 * index + 0] += baseVertex;
			indices[baseIndex + 3 * index + 1] += baseVertex;
			indices[baseIndex + 3 * index + 2] += baseVertex;
		}
	}

	for (int index = 0; index < indices.size(); index++) {
		fwrite(&indices[index], sizeof(indices[index]), 1, pFile);
	}

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
//			fwrite(&vertices[index].color.w, sizeof(vertices[index].color.w), 1, pFile);
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

	return true;
}

bool ExportMesh(const char *szFileName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels)
{
	if (FILE *pFile = fopen(szFileName, "wb")) {
		ExportMeshHeader(pFile, rawModel, rawMaterialModels);
		ExportMeshData(pFile, rawModel, rawMaterialModels);
		fclose(pFile);
	}

	return true;
}

static bool ExportMaterial(const char *szFileName, const RawMaterial &material, const RawModel &rawModel)
{
	TiXmlDocument doc;
	TiXmlElement *pMaterialNode = new TiXmlElement("Material");
	{
		TiXmlElement *pPassNode = new TiXmlElement("Pass");
		{
			pPassNode->SetAttributeString("name", "Default");

			TiXmlElement *pPipelineNode = new TiXmlElement("Pipeline");
			{
				TiXmlElement *pVertexNode = new TiXmlElement("Vertex");
				{
					pVertexNode->SetAttributeString("file_name", "Default.glsl");
				}
				pPipelineNode->LinkEndChild(pVertexNode);

				TiXmlElement *pFragmentNode = new TiXmlElement("Fragment");
				{
					pFragmentNode->SetAttributeString("file_name", "Default.glsl");
				}
				pPipelineNode->LinkEndChild(pFragmentNode);
			}
			pPassNode->LinkEndChild(pPipelineNode);

			for (int index = 0; index < RAW_TEXTURE_USAGE_MAX; index++) {
				if (material.textures[index] == -1) {
					continue;
				}

				TiXmlElement *pTextureNode = new TiXmlElement("Texture2D");
				{
					char szExt[_MAX_PATH];
					char szFName[_MAX_PATH];
					char szFileName[_MAX_PATH];
					splitfilename(rawModel.GetTexture(material.textures[index]).fileName.c_str(), szFName, szExt);
					sprintf(szFileName, "%s%s", szFName, szExt);

					switch (index) {
					case RAW_TEXTURE_USAGE_AMBIENT:   pTextureNode->SetAttributeString("name", "%s", "texAmbient");    break;
					case RAW_TEXTURE_USAGE_DIFFUSE:   pTextureNode->SetAttributeString("name", "%s", "texAlbedo");     break;
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
					pTextureNode->SetAttributeString("min_filter", "%s", "GFX_NEAREST");
					pTextureNode->SetAttributeString("mag_filter", "%s", "GFX_LINEAR");
					pTextureNode->SetAttributeString("mipmap_mode", "%s", "GFX_NEAREST");
					pTextureNode->SetAttributeString("address_mode", "%s", "GFX_CLAMP_TO_EDGE");
				}
				pPassNode->LinkEndChild(pTextureNode);
			}
		}
		pMaterialNode->LinkEndChild(pPassNode);
	}
	doc.LinkEndChild(pMaterialNode);
	return doc.SaveFile(szFileName);
}

bool ExportMaterial(const char *szPathName, const RawModel &rawModel)
{
	for (int index = 0; index < rawModel.GetMaterialCount(); index++) {
		std::string fileName = GetMaterialFileName(szPathName, rawModel.GetMaterial(index));
		ExportMaterial(fileName.c_str(), rawModel.GetMaterial(index), rawModel);
	}
	return true;
}

static void ExportNodeDraw(TiXmlElement *pParentNode, const RawNode &node, std::unordered_map<long, long> &surfaceMeshs, std::unordered_map<long, std::string> &surfaceMaterials)
{
	if (node.surfaceId != 0) {
		TiXmlElement *pDrawNode = new TiXmlElement("Draw");
		{
			pDrawNode->SetAttributeInt1("index", surfaceMeshs[node.surfaceId]);
			pDrawNode->SetAttributeString("material", "%s", surfaceMaterials[node.surfaceId].c_str());
		}
		pParentNode->LinkEndChild(pDrawNode);
	}
}

static void ExportNode(TiXmlElement *pParentNode, const long id, const RawModel &rawModel, std::unordered_map<long, long> &surfaceMeshs, std::unordered_map<long, std::string> &surfaceMaterials)
{
	TiXmlElement *pCurrentNode = new TiXmlElement("Node");
	{
		const RawNode &node = rawModel.GetNode(rawModel.GetNodeById(id));
		pCurrentNode->SetAttributeString("translation", "%f %f %f", node.translation.x, node.translation.y, node.translation.z);
		pCurrentNode->SetAttributeString("rotation", "%f %f %f %f", node.rotation[1], node.rotation[2], node.rotation[3], node.rotation[0]);
		pCurrentNode->SetAttributeString("scale", "%f %f %f", node.scale.x, node.scale.y, node.scale.z);

		ExportNodeDraw(pCurrentNode, node, surfaceMeshs, surfaceMaterials);

		for (int indexChild = 0; indexChild < node.childIds.size(); indexChild++) {
			ExportNode(pCurrentNode, node.childIds[indexChild], rawModel, surfaceMeshs, surfaceMaterials);
		}
	}
	pParentNode->LinkEndChild(pCurrentNode);
}

bool ExportMeshXML(const char *szFileName, const char *szMeshFileName, const RawModel &rawModel, const std::vector<RawModel> &rawMaterialModels)
{
	std::unordered_map<long, long> surfaceMeshs;
	std::unordered_map<long, std::string> surfaceMaterials;

	for (int indexMesh = 0; indexMesh < rawMaterialModels.size(); indexMesh++) {
		long id = rawMaterialModels[indexMesh].GetSurface(0).id;
		surfaceMeshs[id] = indexMesh;
		surfaceMaterials[id] = GetMaterialFileName("", rawMaterialModels[indexMesh].GetMaterial(0));
	}

	TiXmlDocument doc;
	TiXmlElement *pMeshNode = new TiXmlElement("Mesh");
	pMeshNode->SetAttributeString("mesh", "%s", szMeshFileName);
	{
		ExportNode(pMeshNode, rawModel.GetRootNode(), rawModel, surfaceMeshs, surfaceMaterials);
	}
	doc.LinkEndChild(pMeshNode);
	doc.SaveFile(szFileName);

	return true;
}
