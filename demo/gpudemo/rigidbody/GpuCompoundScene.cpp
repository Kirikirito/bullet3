#include "GpuCompoundScene.h"
#include "GpuRigidBodyDemo.h"
#include "BulletCommon/btQuickprof.h"
#include "OpenGLWindow/ShapeData.h"

#include "OpenGLWindow/GLInstancingRenderer.h"
#include "BulletCommon/btQuaternion.h"
#include "OpenGLWindow/btgWindowInterface.h"
#include "gpu_broadphase/host/btGpuSapBroadphase.h"
#include "../GpuDemoInternalData.h"
#include "basic_initialize/btOpenCLUtils.h"
#include "OpenGLWindow/OpenGLInclude.h"
#include "OpenGLWindow/GLInstanceRendererInternalData.h"
#include "parallel_primitives/host/btLauncherCL.h"
#include "gpu_rigidbody/host/btGpuRigidBodyPipeline.h"
#include "gpu_rigidbody/host/btGpuNarrowPhase.h"
#include "gpu_rigidbody/host/btConfig.h"
#include "GpuRigidBodyDemoInternalData.h"
#include "BulletCommon/btTransform.h"

#include "OpenGLWindow/GLInstanceGraphicsShape.h"



void GpuCompoundScene::setupScene(const ConstructionInfo& ci)
{
	int strideInBytes = 9*sizeof(float);
	int numVertices = sizeof(cube_vertices)/strideInBytes;
	int numIndices = sizeof(cube_indices)/sizeof(int);

	btAlignedObjectArray<GLInstanceVertex> vertexArray;
	btAlignedObjectArray<int> indexArray;
	

	//int shapeId = ci.m_instancingRenderer->registerShape(&cube_vertices[0],numVertices,cube_indices,numIndices);
	int group=1;
	int mask=1;
	int index=0;
	float scaling[4] = {1,1,1,1};
	int colIndex = 0;

	{
		if (1)
	{
			float radius = 41;
			int prevGraphicsShapeIndex = -1;
		{

			
		
			if (radius>=100)
			{
				int numVertices = sizeof(detailed_sphere_vertices)/strideInBytes;
				int numIndices = sizeof(detailed_sphere_indices)/sizeof(int);
				prevGraphicsShapeIndex = ci.m_instancingRenderer->registerShape(&detailed_sphere_vertices[0],numVertices,detailed_sphere_indices,numIndices);
			} else
			{
				bool usePointSprites = false;
				if (usePointSprites)
				{
					int numVertices = sizeof(point_sphere_vertices)/strideInBytes;
					int numIndices = sizeof(point_sphere_indices)/sizeof(int);
					prevGraphicsShapeIndex = ci.m_instancingRenderer->registerShape(&point_sphere_vertices[0],numVertices,point_sphere_indices,numIndices,BT_GL_POINTS);
				} else
				{
					if (radius>=10)
					{
						int numVertices = sizeof(medium_sphere_vertices)/strideInBytes;
						int numIndices = sizeof(medium_sphere_indices)/sizeof(int);
						prevGraphicsShapeIndex = ci.m_instancingRenderer->registerShape(&medium_sphere_vertices[0],numVertices,medium_sphere_indices,numIndices);
					} else
					{
						int numVertices = sizeof(low_sphere_vertices)/strideInBytes;
						int numIndices = sizeof(low_sphere_indices)/sizeof(int);
						prevGraphicsShapeIndex = ci.m_instancingRenderer->registerShape(&low_sphere_vertices[0],numVertices,low_sphere_indices,numIndices);
					}
				}
			}

		}
		btVector4 colors[4] = 
		{
			btVector4(1,0,0,1),
			btVector4(0,1,0,1),
			btVector4(0,1,1,1),
			btVector4(1,1,0,1),
		};

		int curColor = 1;

		//int colIndex = m_data->m_np->registerConvexHullShape(&cube_vertices[0],strideInBytes,numVertices, scaling);
		int colIndex = m_data->m_np->registerSphereShape(radius);//>registerConvexHullShape(&cube_vertices[0],strideInBytes,numVertices, scaling);
		float mass = 0.f;

		//btVector3 position((j&1)+i*2.2,1+j*2.,(j&1)+k*2.2);
		btVector3 position(0,-41,0);

		
		btQuaternion orn(0,0,0,1);

		btVector4 color = colors[curColor];
		curColor++;
		curColor&=3;
		btVector4 scaling(radius,radius,radius,1);
		int id = ci.m_instancingRenderer->registerGraphicsInstance(prevGraphicsShapeIndex,position,orn,color,scaling);
		int pid = m_data->m_rigidBodyPipeline->registerPhysicsInstance(mass,position,orn,colIndex,index);

		index++;


	}
	}
	GLInstanceVertex* cubeVerts = (GLInstanceVertex*)&cube_vertices[0];
	int stride2 = sizeof(GLInstanceVertex);
	btAssert(stride2 == strideInBytes);

	{
		int childColIndex = m_data->m_np->registerConvexHullShape(&cube_vertices[0],strideInBytes,numVertices, scaling);
		

		btVector3 childPositions[3] = {
			btVector3(0,-2,0),
			btVector3(0,0,0),
			btVector3(0,2,0)
		};

		
		btAlignedObjectArray<btGpuChildShape> childShapes;
		int numChildShapes = 3;
		for (int i=0;i<numChildShapes;i++)
		{
			//for now, only support polyhedral child shapes
			btGpuChildShape child;
			child.m_shapeIndex = childColIndex;
			btVector3 pos = childPositions[i];
			btQuaternion orn(0,0,0,1);
			for (int v=0;v<4;v++)
			{
				child.m_childPosition[v] = pos[v];
				child.m_childOrientation[v] = orn[v];
			}
			childShapes.push_back(child);
			btTransform tr;
			tr.setIdentity();
			tr.setOrigin(pos);
			tr.setRotation(orn);

			int baseIndex = vertexArray.size();
			for (int j=0;j<numIndices;j++)
				indexArray.push_back(cube_indices[j]+baseIndex);

			//add transformed graphics vertices and indices
			for (int v=0;v<numVertices;v++)
			{
				GLInstanceVertex vert = cubeVerts[v];
				btVector3 vertPos(vert.xyzw[0],vert.xyzw[1],vert.xyzw[2]);
				btVector3 newPos = tr*vertPos;
				vert.xyzw[0] = newPos[0];
				vert.xyzw[1] = newPos[1];
				vert.xyzw[2] = newPos[2];
				vert.xyzw[3] = 0.f;
				vertexArray.push_back(vert);
			}

		}
		colIndex= m_data->m_np->registerCompoundShape(&childShapes);
		
	}

	//int shapeId = ci.m_instancingRenderer->registerShape(&cube_vertices[0],numVertices,cube_indices,numIndices);
	int shapeId = ci.m_instancingRenderer->registerShape(&vertexArray[0].xyzw[0],vertexArray.size(),&indexArray[0],indexArray.size());

	btVector4 colors[4] = 
	{
		btVector4(1,0,0,1),
		btVector4(0,1,0,1),
		btVector4(0,0,1,1),
		btVector4(0,1,1,1),
	};
		
	int curColor = 0;
	for (int i=0;i<ci.arraySizeX;i++)
	{
		for (int j=0;j<ci.arraySizeY;j++)
		{
			for (int k=0;k<ci.arraySizeZ;k++)
			{
				float mass = 1;//j==0? 0.f : 1.f;

				btVector3 position(i*ci.gapX,10+j*ci.gapY,k*ci.gapZ);
				//btQuaternion orn(0,0,0,1);
				btQuaternion orn(btVector3(1,0,0),0.7);
				
				btVector4 color = colors[curColor];
				curColor++;
				curColor&=3;
				btVector4 scaling(1,1,1,1);
				int id = ci.m_instancingRenderer->registerGraphicsInstance(shapeId,position,orn,color,scaling);
				int pid = m_data->m_rigidBodyPipeline->registerPhysicsInstance(mass,position,orn,colIndex,index);
				
				index++;
			}
		}
	}

	float camPos[4]={0,0,0};//65.5,4.5,65.5,0};
	//float camPos[4]={1,12.5,1.5,0};
	m_instancingRenderer->setCameraTargetPosition(camPos);
	m_instancingRenderer->setCameraDistance(20);
	
}




